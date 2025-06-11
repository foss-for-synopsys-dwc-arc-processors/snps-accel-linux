// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include "snps_accel_drv.h"

#define MAILBOX_RPM_SENDER	0x5a5eU

/**
 * snps_accel_mbox_send_message - Send a message.
 * @system: System.
 * @msg: Message.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_mbox_send_message(struct snps_accel_system *system,
				 struct snps_accel_mbox_msg *msg)
{
	return snps_accel_arcsync_send_irq(system->mgr, MAILBOX_RPM_SENDER,
					   msg->processor_id, msg->core_id, 0);
}

/**
 * snps_accel_mbox_abort_message - Abort a sent message.
 * @system: System.
 * @msg: Message.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_mbox_abort_message(struct snps_accel_system *system,
				  struct snps_accel_mbox_msg *msg)
{
	snps_accel_arcsync_abort_irq(system->mgr, msg->processor_id,
				     msg->core_id, 0);
	return 0;
}
