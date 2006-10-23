/******************************************************************************
 *
 *  $Id$
 *
 *  Copyright (C) 2006  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  The right to use EtherCAT Technology is granted and comes free of
 *  charge under condition of compatibility of product made by
 *  Licensee. People intending to distribute/sell products based on the
 *  code, have to sign an agreement to guarantee that products using
 *  software based on IgH EtherCAT master stay compatible with the actual
 *  EtherCAT specification (which are released themselves as an open
 *  standard) as the (only) precondition to have the right to use EtherCAT
 *  Technology, IP and trade marks.
 *
 *****************************************************************************/

/**
   \file
   EtherCAT state change FSM.
*/

/*****************************************************************************/

#include "globals.h"
#include "master.h"
#include "fsm_change.h"

/*****************************************************************************/

void ec_fsm_change_start(ec_fsm_change_t *);
void ec_fsm_change_check(ec_fsm_change_t *);
void ec_fsm_change_status(ec_fsm_change_t *);
void ec_fsm_change_code(ec_fsm_change_t *);
void ec_fsm_change_ack(ec_fsm_change_t *);
void ec_fsm_change_check_ack(ec_fsm_change_t *);
void ec_fsm_change_end(ec_fsm_change_t *);
void ec_fsm_change_error(ec_fsm_change_t *);

/*****************************************************************************/

/**
   Constructor.
*/

void ec_fsm_change_init(ec_fsm_change_t *fsm, /**< finite state machine */
                        ec_datagram_t *datagram /**< datagram */
                        )
{
    fsm->state = NULL;
    fsm->datagram = datagram;
}

/*****************************************************************************/

/**
   Destructor.
*/

void ec_fsm_change_clear(ec_fsm_change_t *fsm /**< finite state machine */)
{
}

/*****************************************************************************/

/**
   Resets the state machine.
*/

void ec_fsm_change(ec_fsm_change_t *fsm, /**< finite state machine */
                   ec_slave_t *slave, /**< EtherCAT slave */
                   ec_slave_state_t state /**< requested state */
                   )
{
    fsm->slave = slave;
    fsm->requested_state = state;
    fsm->state = ec_fsm_change_start;
}

/*****************************************************************************/

/**
   Executes the current state of the state machine.
*/

void ec_fsm_change_exec(ec_fsm_change_t *fsm /**< finite state machine */)
{
    fsm->state(fsm);
}

/*****************************************************************************/

/**
   Returns the running state of the state machine.
   \return non-zero if not terminated yet.
*/

int ec_fsm_change_running(ec_fsm_change_t *fsm /**< Finite state machine */)
{
    return fsm->state != ec_fsm_change_end
        && fsm->state != ec_fsm_change_error;
}

/*****************************************************************************/

/**
   Returns, if the state machine terminated with success.
   \return non-zero if successful.
*/

int ec_fsm_change_success(ec_fsm_change_t *fsm /**< Finite state machine */)
{
    return fsm->state == ec_fsm_change_end;
}

/******************************************************************************
 *  state change state machine
 *****************************************************************************/

/**
   Change state: START.
*/

void ec_fsm_change_start(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    fsm->take_time = 1;

    // write new state to slave
    ec_datagram_npwr(datagram, slave->station_address, 0x0120, 2);
    EC_WRITE_U16(datagram->data, fsm->requested_state);
    ec_master_queue_datagram(fsm->slave->master, datagram);
    fsm->state = ec_fsm_change_check;
}

/*****************************************************************************/

/**
   Change state: CHECK.
*/

void ec_fsm_change_check(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_change_error;
        EC_ERR("Failed to send state datagram to slave %i!\n",
               fsm->slave->ring_position);
        return;
    }

    if (fsm->take_time) {
        fsm->take_time = 0;
        fsm->jiffies_start = datagram->jiffies_sent;
    }

    if (datagram->working_counter != 1) {
        if (datagram->jiffies_received - fsm->jiffies_start >= 3 * HZ) {
            fsm->state = ec_fsm_change_error;
            EC_ERR("Failed to set state 0x%02X on slave %i: Slave did not"
                   " respond.\n", fsm->requested_state,
                   fsm->slave->ring_position);
            return;
        }

        // repeat writing new state to slave
        ec_datagram_npwr(datagram, slave->station_address, 0x0120, 2);
        EC_WRITE_U16(datagram->data, fsm->requested_state);
        ec_master_queue_datagram(fsm->slave->master, datagram);
        return;
    }

    fsm->take_time = 1;

    // read AL status from slave
    ec_datagram_nprd(datagram, slave->station_address, 0x0130, 2);
    ec_master_queue_datagram(fsm->slave->master, datagram);
    fsm->state = ec_fsm_change_status;
}

/*****************************************************************************/

/**
   Change state: STATUS.
*/

void ec_fsm_change_status(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state != EC_DATAGRAM_RECEIVED
        || datagram->working_counter != 1) {
        fsm->state = ec_fsm_change_error;
        EC_ERR("Failed to check state 0x%02X on slave %i.\n",
               fsm->requested_state, slave->ring_position);
        return;
    }

    if (fsm->take_time) {
        fsm->take_time = 0;
        fsm->jiffies_start = datagram->jiffies_sent;
    }

    slave->current_state = EC_READ_U8(datagram->data);

    if (slave->current_state == fsm->requested_state) {
        // state has been set successfully
        fsm->state = ec_fsm_change_end;
        return;
    }

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        // state change error
        EC_ERR("Failed to set state 0x%02X - Slave %i refused state change"
               " (code 0x%02X)!\n", fsm->requested_state, slave->ring_position,
               slave->current_state);
        // fetch AL status error code
        ec_datagram_nprd(datagram, slave->station_address, 0x0134, 2);
        ec_master_queue_datagram(fsm->slave->master, datagram);
        fsm->state = ec_fsm_change_code;
        return;
    }

    if (datagram->jiffies_received
        - fsm->jiffies_start >= 100 * HZ / 1000) { // 100ms
        // timeout while checking
        fsm->state = ec_fsm_change_error;
        EC_ERR("Timeout while setting state 0x%02X on slave %i.\n",
               fsm->requested_state, slave->ring_position);
        return;
    }

    // still old state: check again
    ec_datagram_nprd(datagram, slave->station_address, 0x0130, 2);
    ec_master_queue_datagram(fsm->slave->master, datagram);
}

/*****************************************************************************/

/**
   Application layer status messages.
*/

const ec_code_msg_t al_status_messages[] = {
    {0x0001, "Unspecified error"},
    {0x0011, "Invalud requested state change"},
    {0x0012, "Unknown requested state"},
    {0x0013, "Bootstrap not supported"},
    {0x0014, "No valid firmware"},
    {0x0015, "Invalid mailbox configuration"},
    {0x0016, "Invalid mailbox configuration"},
    {0x0017, "Invalid sync manager configuration"},
    {0x0018, "No valid inputs available"},
    {0x0019, "No valid outputs"},
    {0x001A, "Synchronisation error"},
    {0x001B, "Sync manager watchdog"},
    {0x001C, "Invalid sync manager types"},
    {0x001D, "Invalid output configuration"},
    {0x001E, "Invalid input configuration"},
    {0x001F, "Invalid watchdog configuration"},
    {0x0020, "Slave needs cold start"},
    {0x0021, "Slave needs INIT"},
    {0x0022, "Slave needs PREOP"},
    {0x0023, "Slave needs SAVEOP"},
    {0x0030, "Invalid DC SYNCH configuration"},
    {0x0031, "Invalid DC latch configuration"},
    {0x0032, "PLL error"},
    {0x0033, "Invalid DC IO error"},
    {0x0034, "Invalid DC timeout error"},
    {0x0042, "MBOX EOE"},
    {0x0043, "MBOX COE"},
    {0x0044, "MBOX FOE"},
    {0x0045, "MBOX SOE"},
    {0x004F, "MBOX VOE"},
    {}
};

/*****************************************************************************/

/**
   Change state: CODE.
*/

void ec_fsm_change_code(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    uint32_t code;
    const ec_code_msg_t *al_msg;

    if (datagram->state != EC_DATAGRAM_RECEIVED
        || datagram->working_counter != 1) {
        EC_WARN("Reception of AL status code datagram failed.\n");
    }
    else {
        if ((code = EC_READ_U16(datagram->data))) {
            for (al_msg = al_status_messages; al_msg->code; al_msg++) {
                if (al_msg->code != code) continue;
                EC_ERR("AL status message 0x%04X: \"%s\".\n",
                       al_msg->code, al_msg->message);
                break;
            }
            if (!al_msg->code)
                EC_ERR("Unknown AL status code 0x%04X.\n", code);
        }
    }

    // acknowledge "old" slave state
    ec_datagram_npwr(datagram, slave->station_address, 0x0120, 2);
    EC_WRITE_U16(datagram->data, slave->current_state);
    ec_master_queue_datagram(fsm->slave->master, datagram);
    fsm->state = ec_fsm_change_ack;
}

/*****************************************************************************/

/**
   Change state: ACK.
*/

void ec_fsm_change_ack(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state != EC_DATAGRAM_RECEIVED
        || datagram->working_counter != 1) {
        fsm->state = ec_fsm_change_error;
        EC_ERR("Reception of state ack datagram failed.\n");
        return;
    }

    fsm->take_time = 1;

    // read new AL status
    ec_datagram_nprd(datagram, slave->station_address, 0x0130, 2);
    ec_master_queue_datagram(fsm->slave->master, datagram);
    fsm->state = ec_fsm_change_check_ack;
}

/*****************************************************************************/

/**
   Change state: CHECK ACK.
*/

void ec_fsm_change_check_ack(ec_fsm_change_t *fsm /**< finite state machine */)
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state != EC_DATAGRAM_RECEIVED
        || datagram->working_counter != 1) {
        fsm->state = ec_fsm_change_error;
        EC_ERR("Reception of state ack check datagram failed.\n");
        return;
    }

    if (fsm->take_time) {
        fsm->take_time = 0;
        fsm->jiffies_start = datagram->jiffies_sent;
    }

    slave->current_state = EC_READ_U8(datagram->data);

    if (!(slave->current_state & EC_SLAVE_STATE_ACK_ERR)) {
        fsm->state = ec_fsm_change_error;
        EC_INFO("Acknowledged state 0x%02X on slave %i.\n",
                slave->current_state, slave->ring_position);
        return;
    }

    if (datagram->jiffies_received
        - fsm->jiffies_start >= 100 * HZ / 1000) { // 100ms
        // timeout while checking
        slave->current_state = EC_SLAVE_STATE_UNKNOWN;
        fsm->state = ec_fsm_change_error;
        EC_ERR("Timeout while acknowledging state 0x%02X on slave %i.\n",
               fsm->requested_state, slave->ring_position);
        return;
    }

    // reread new AL status
    ec_datagram_nprd(datagram, slave->station_address, 0x0130, 2);
    ec_master_queue_datagram(fsm->slave->master, datagram);
}

/*****************************************************************************/

/**
   State: ERROR.
*/

void ec_fsm_change_error(ec_fsm_change_t *fsm /**< finite state machine */)
{
}

/*****************************************************************************/

/**
   State: END.
*/

void ec_fsm_change_end(ec_fsm_change_t *fsm /**< finite state machine */)
{
}

/*****************************************************************************/