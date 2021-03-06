/* 
 * Copyright (C) 2012-     Darren Kulp (http://kulp.ch/)
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#define PJMEDIA_CONF_USE_SWITCH_BOARD 1
#include <pjsua-lib/pjsua.h>

#define THIS_FILE       "APP"

struct mydata {
    pjsua_conf_port_id play_port;
    pjsua_conf_port_id rec_port;
};

/* Callback called by the library upon receiving incoming call */
static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
                             pjsip_rx_data *rdata)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);

    pjsua_call_get_info(call_id, &ci);

    PJ_LOG(3,(THIS_FILE, "Incoming call from %.*s!!",
                         (int)ci.remote_info.slen,
                         ci.remote_info.ptr));

    /* Automatically answer incoming calls with 200/OK */
    pjsua_call_answer(call_id, 200, NULL, NULL);
}

/* Callback called by the library when call's state has changed */
static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(e);

    pjsua_call_get_info(call_id, &ci);
    PJ_LOG(3,(THIS_FILE, "Call %d state=%.*s", call_id,
                         (int)ci.state_text.slen,
                         ci.state_text.ptr));
}

/* Callback called by the library when call's media state has changed */
static void on_call_media_state(pjsua_call_id call_id)
{
    pjsua_call_info ci;

    pjsua_call_get_info(call_id, &ci);
    struct mydata *me = pjsua_acc_get_user_data(ci.acc_id);

    if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
        pjsua_conf_connect(me->play_port, ci.conf_slot);
        pjsua_conf_connect(ci.conf_slot, me->rec_port);
    }
}

/* Display error and exit application */
static void error_exit(const char *title, pj_status_t status)
{
    pjsua_perror(THIS_FILE, title, status);
    pjsua_destroy();
    exit(1);
}

static int set_up_wav_player(pjsua_conf_port_id *wav_port)
{
    pjsua_player_id wav_id;
    unsigned play_options = PJMEDIA_FILE_NO_LOOP;
    pj_str_t str = pj_str("data.wav");
    pj_status_t status = pjsua_player_create(&str,
            play_options, &wav_id);
    if (status != PJ_SUCCESS)
        return 1;

    *wav_port = pjsua_player_get_conf_port(wav_id);

    return 0;
}

static int set_up_wav_recorder(pjsua_conf_port_id *wav_port)
{
    pjsua_recorder_id wav_id;
    unsigned record_options = 0;
    pj_str_t str = pj_str("data-out.wav");
    pj_status_t status = pjsua_recorder_create(&str,
            0, NULL, -1, record_options, &wav_id);
    if (status != PJ_SUCCESS)
        return 1;

    *wav_port = pjsua_recorder_get_conf_port(wav_id);

    return 0;
}

/*
 * main()
 *
 * argv[1] may contain URL to call.
 */
int main(int argc, char *argv[])
{
    pjsua_acc_id acc_id;
    pj_status_t status;

    struct mydata me = { .play_port = 0 };

    /* Create pjsua first! */
    status = pjsua_create();
    if (status != PJ_SUCCESS) error_exit("Error in pjsua_create()", status);

    /* If argument is specified, it's got to be a valid SIP URL */
    if (argc > 1) {
        status = pjsua_verify_url(argv[1]);
        if (status != PJ_SUCCESS) error_exit("Invalid URL in argv", status);
    }

    /* Init pjsua */
    {
        pjsua_config cfg;
        pjsua_logging_config log_cfg;

        pjsua_config_default(&cfg);
        cfg.cb.on_incoming_call = &on_incoming_call;
        cfg.cb.on_call_media_state = &on_call_media_state;
        cfg.cb.on_call_state = &on_call_state;

        cfg.stun_host = pj_str(STUN_SERVER);
        if (cfg.stun_srv_cnt==PJ_ARRAY_SIZE(cfg.stun_srv)) {
            PJ_LOG(1,(THIS_FILE, "Error: too many STUN servers"));
            return PJ_ETOOMANY;
        }
        cfg.stun_srv[cfg.stun_srv_cnt++] = pj_str(STUN_SERVER);

        pjsua_logging_config_default(&log_cfg);
        log_cfg.console_level = 4;

        status = pjsua_init(&cfg, &log_cfg, NULL);
        if (status != PJ_SUCCESS) error_exit("Error in pjsua_init()", status);
    }

    /* Add UDP transport. */
    {
        pjsua_transport_config cfg;

        pjsua_transport_config_default(&cfg);
        cfg.port = 5060;
        status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, NULL);
        if (status != PJ_SUCCESS) error_exit("Error creating transport", status);
    }

    /* Initialization is done, now start pjsua */
    status = pjsua_start();
    if (status != PJ_SUCCESS) error_exit("Error starting pjsua", status);

    /* Register to SIP server by creating SIP account. */
    {
        pjsua_acc_config cfg;

        pjsua_acc_config_default(&cfg);
        cfg.id = pj_str("sip:" SIP_USER "@" SIP_DOMAIN);
        cfg.reg_uri = pj_str("sip:" SIP_DOMAIN);
        cfg.cred_count = 1;
        cfg.cred_info[0].realm = pj_str(SIP_DOMAIN);
        cfg.cred_info[0].scheme = pj_str("digest");
        cfg.cred_info[0].username = pj_str(SIP_USER);
        cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
        cfg.cred_info[0].data = pj_str(SIP_PASSWD);

        status = pjsua_acc_add(&cfg, PJ_TRUE, &acc_id);
        if (status != PJ_SUCCESS) error_exit("Error adding account", status);
        pjsua_acc_set_user_data(acc_id, &me);
    }

    pjsua_conf_port_id play_port, rec_port;
    int rc = 0;
    rc = set_up_wav_player(&play_port);
    rc = set_up_wav_recorder(&rec_port);
    me.play_port = play_port;
    me.rec_port = rec_port;

    /* If URL is specified, make call to the URL. */
    if (argc > 1) {
        pj_str_t uri = pj_str(argv[1]);
        status = pjsua_call_make_call(acc_id, &uri, 0, NULL, NULL, NULL);
        if (status != PJ_SUCCESS) error_exit("Error making call", status);
    }

    /* Wait until user press "q" to quit. */
    for (;;) {
        char option[10];

        puts("Press 'h' to hangup all calls, 'q' to quit");
        if (fgets(option, sizeof(option), stdin) == NULL) {
            puts("EOF while reading stdin, will quit now..");
            break;
        }

        if (option[0] == 'q')
            break;

        if (option[0] == 'h')
            pjsua_call_hangup_all();
    }

    /* Destroy pjsua */
    pjsua_destroy();

    return 0;
}

/* vi:set ts=4 sw=4 tw=80: */

