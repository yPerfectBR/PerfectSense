// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — Phase 0 stub.
 * Real WMI probe/sysfs lands in roadmap Phase 1+.
 * Licensing: kmod/ is GPL-2.0-or-later; userspace is AGPL-3.0 — see
 * docs/en/architecture/licensing.md
 */

#include <linux/module.h>
#include <linux/kernel.h>

static int __init perfectsense_init(void)
{
	pr_info("perfectsense: stub loaded (no WMI bind yet)\n");
	return 0;
}

static void __exit perfectsense_exit(void)
{
	pr_info("perfectsense: stub unloaded\n");
}

module_init(perfectsense_init);
module_exit(perfectsense_exit);

MODULE_AUTHOR("PerfectSense authors");
MODULE_DESCRIPTION("PerfectSense stub (PHN16-72-74VX)");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.0");
