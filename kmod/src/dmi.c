// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — DMI allowlist (PHN16-72 focus).
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/string.h>

#include "perfectsense.h"

bool ps_dmi_supported(void)
{
	const char *product = dmi_get_system_info(DMI_PRODUCT_NAME);
	const char *family = dmi_get_system_info(DMI_PRODUCT_FAMILY);

	if (product && strstr(product, "PHN16-72"))
		return true;

	/* Fallback: some firmware strings omit the SKU token in PRODUCT_NAME */
	if (family && strstr(family, "Helios Neo 16") &&
	    product && strstr(product, "Predator"))
		return true;

	return false;
}
