// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2019 Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/pci.h>
#include <linux/version.h>

void mt76_pci_disable_aspm(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev->bus->self;
	u16 aspm_conf, parent_aspm_conf = 0;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &aspm_conf);
	aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	if (parent) {
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_aspm_conf);
		parent_aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	}

	if (!aspm_conf && (!parent || !parent_aspm_conf)) {
		/* aspm already disabled */
		return;
	}

	dev_info(&pdev->dev, "disabling ASPM %s %s\n",
		 (aspm_conf & PCI_EXP_LNKCTL_ASPM_L0S) ? "L0s" : "",
		 (aspm_conf & PCI_EXP_LNKCTL_ASPM_L1) ? "L1" : "");

	if (IS_ENABLED(CONFIG_PCIEASPM)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
		int err;

		err = pci_disable_link_state(pdev, aspm_conf);
		if (!err)
			return;
#else
		pci_disable_link_state(pdev, aspm_conf);
#endif
	}

	/* both device and parent should have the same ASPM setting.
	 * disable ASPM in downstream component first and then upstream.
	 */
	pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL, aspm_conf);
	if (parent)
		pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
					   aspm_conf);
}
EXPORT_SYMBOL_GPL(mt76_pci_disable_aspm);
