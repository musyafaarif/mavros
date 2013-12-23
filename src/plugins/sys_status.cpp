/**
 * @file sys_status.cpp
 * @author Vladimit Ermkov <voon341@gmail.com>
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <mavros/mavros_plugin.h>
#include <pluginlib/class_list_macros.h>

namespace mavplugin {

/**
 * Heartbeat status publisher
 *
 * Based on diagnistic_updater::FrequencyStatus
 */
class HeartbeatStatus : public diagnostic_updater::DiagnosticTask
{
public:
	HeartbeatStatus(const std::string name, size_t win_size) :
		diagnostic_updater::DiagnosticTask(name),
		window_size_(win_size),
		min_freq_(0.2),
		max_freq_(100),
		tolerance_(0.1),
		times_(win_size),
		seq_nums_(win_size)
	{
		clear();
	}

	void clear() {
		boost::mutex::scoped_lock lock(lock_);
		ros::Time curtime = ros::Time::now();
		count_ = 0;

		for (int i = 0; i < window_size_; i++)
		{
			times_[i] = curtime;
			seq_nums_[i] = count_;
		}

		hist_indx_ = 0;
	}

	void tick(mavlink_heartbeat_t &hb_struct) {
		boost::mutex::scoped_lock lock(lock_);
		count_++;
		last_hb = hb_struct;
	}

	virtual void run(diagnostic_updater::DiagnosticStatusWrapper &stat) {
		boost::mutex::scoped_lock lock(lock_);
		ros::Time curtime = ros::Time::now();
		int curseq = count_;
		int events = curseq - seq_nums_[hist_indx_];
		double window = (curtime - times_[hist_indx_]).toSec();
		double freq = events / window;
		seq_nums_[hist_indx_] = curseq;
		times_[hist_indx_] = curtime;
		hist_indx_ = (hist_indx_ + 1) % window_size_;

		if (events == 0) {
			stat.summary(2, "No events recorded.");
		}
		else if (freq < min_freq_ * (1 - tolerance_)) {
			stat.summary(1, "Frequency too low.");
		}
		else if (freq > max_freq_ * (1 + tolerance_)) {
			stat.summary(1, "Frequency too high.");
		}
		else {
			stat.summary(0, "Normal");
		}

		stat.addf("Events in window", "%d", events);
		stat.addf("Events since startup", "%d", count_);
		stat.addf("Duration of window (s)", "%f", window);
		stat.addf("Actual frequency (Hz)", "%f", freq);
		stat.addf("MAV Type", "%u", last_hb.type);
		stat.addf("Autopilot type", "%u", last_hb.autopilot);
		stat.addf("Autopilot base mode", "0x%02X", last_hb.base_mode);
		stat.addf("Autopilot custom mode", "0x%08X", last_hb.custom_mode);
		stat.addf("Autopilot system status", "%u", last_hb.system_status);
	}

private:
	int count_;
	std::vector<ros::Time> times_;
	std::vector<int> seq_nums_;
	int hist_indx_;
	boost::mutex lock_;
	const size_t window_size_;
	const double min_freq_;
	const double max_freq_;
	const double tolerance_;
	mavlink_heartbeat_t last_hb;
};


class SystemStatusPlugin : public MavRosPlugin
{
public:
	SystemStatusPlugin() :
		hb_diag("FCU Heartbeat", 10)
	{};

	void initialize(ros::NodeHandle &nh,
			const boost::shared_ptr<mavconn::MAVConnInterface> &mav_link,
			diagnostic_updater::Updater &diag_updater) {

		diag_updater.add(hb_diag);
	}

	std::string get_name() {
		return "SystemStatus";
	}

	std::vector<uint8_t> get_supported_messages() {
		return {
			MAVLINK_MSG_ID_HEARTBEAT,
			MAVLINK_MSG_ID_SYSTEM_TIME,
			MAVLINK_MSG_ID_SYS_STATUS
		};
	}

	void message_rx_cb(const mavlink_message_t *msg, uint8_t sysid, uint8_t compid) {
		switch (msg->msgid) {
		case MAVLINK_MSG_ID_HEARTBEAT:
			{
				mavlink_heartbeat_t hb;
				mavlink_msg_heartbeat_decode(msg, &hb);
				hb_diag.tick(hb);
			}
			break;

		case MAVLINK_MSG_ID_SYSTEM_TIME:
			break;

		case MAVLINK_MSG_ID_SYS_STATUS:
			break;
		};
	}

private:
	HeartbeatStatus hb_diag;
};

}; // namespace mavplugin

PLUGINLIB_EXPORT_CLASS(mavplugin::SystemStatusPlugin, mavplugin::MavRosPlugin)

