`ddclight` is a daemon and command-line client to change screen brightness with DDC/CI.

It's able to be more responsive than some existing tools by daemonizing and holding open file descriptors to the i2c devices and by ignoring (rather than enqueueing) commands received faster than they can be executed.  It's also designed to coordinate multiple-monitor setups.
