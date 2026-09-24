#
# Configuration file for ew_controller (graphical control panel for startstop)
#
# This module does NOT consume data rings: it uses a dedicated control ring
# (CONTROL_RING) through which it exchanges TYPE_REQSTATUS / TYPE_STATUS /
# TYPE_STOP / TYPE_RESTART / TYPE_RECONFIG messages with the Earthworm manager (startstop).

MyModuleId      MOD_CONTROL       # Module ID (must be registered in earthworm.d)
Ring            CONTROL_RING      # Dedicated control ring (key 1060, size 512 kb)
HeartBeatInt    30                # Heartbeat interval (seconds)
PollInt         5                 # Status request interval to startstop (seconds)
LogFile         1                 # 1 = Write log to disk, 0 = Console only

# LogDir (OPTIONAL): directory where the log files shown by the viewer are.
# It is resolved with priority: EW_LOG environment variable > this value > "logs".
# Normally it does not need to be defined: if the system is started with ew_unix.sh,
# EW_LOG already points to the correct directory (e.g. logs/ of the installation).
#LogDir         "logs"