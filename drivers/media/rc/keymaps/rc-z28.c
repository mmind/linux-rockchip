/*
 * Keytable for the Z28 TV-box remote controller
 *
 * Copyright (C) 2018 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table z28[] = {
	{ 0x101, KEY_POWER },
	{ 0x102, KEY_MUTE },
	{ 0x103, KEY_UP },
	{ 0x105, KEY_LEFT },
	{ 0x106, KEY_RIGHT },
	{ 0x107, KEY_OK },
	{ 0x108, KEY_DOWN },
	{ 0x125, KEY_BACK },
	{ 0x126, KEY_MENU },
	{ 0x127, KEY_HOME },
	{ 0x128, KEY_VOLUMEUP },
	{ 0x129, KEY_VOLUMEDOWN },
};

static struct rc_map_list z28_map = {
	.map = {
		.scan     = z28,
		.size     = ARRAY_SIZE(z28),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_Z28,
	}
};

static int __init init_rc_map_z28(void)
{
	return rc_map_register(&z28_map);
}

static void __exit exit_rc_map_z28(void)
{
	rc_map_unregister(&z28_map);
}

module_init(init_rc_map_z28)
module_exit(exit_rc_map_z28)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
