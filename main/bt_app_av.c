#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/param.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"

#include "sys/lock.h"

#include "nvs_devices.h"
#include "display.h"
#include "led.h"


// AVRCP used transaction label
#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_GET_META_DATA       (1)
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)

/* a2dp event handler */
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);
/* avrc CT event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);
/* avrc TG event handler */
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param);

static uint32_t s_pkt_cnt = 0;
static esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;
static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Stopped", "Started"};
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;
static _lock_t s_volume_lock;
static uint8_t s_volume = 0;
static bool s_volume_notify;
static bool s_volume_notify_disabled = false;
static uint32_t i_volume = 32;

extern uint8_t *remote_name;


/* callback for A2DP sink */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT: {
        bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    static uint8_t *da_data = NULL;
    static size_t da_len = 0;
    static const uint8_t byte_per_sample = 2;
    static const uint8_t byte_per_da_sample = 4;
    static int16_t sample;
    static int32_t da_sample;
    static uint32_t level[2];
    static uint8_t lr;

    if (len % byte_per_sample != 0) ESP_LOGE(BT_AV_TAG, "data unaligned: %u", len);

    if (da_data == NULL) {
        //allocate 4 times as needed to reduce dangerous reallocate
        da_len = len << 3;
        da_data = (uint8_t *)malloc(da_len);
        ESP_LOGI(BT_AV_TAG, "allocated da buffer memory: %u bytes", da_len);
    }
    if (da_len < len << 1) {
        //allocate 4 times as needed to reduce dangerous reallocate
        da_len = len << 3;
        realloc(da_data, da_len);
        ESP_LOGI(BT_AV_TAG, "reallocated da buffer memory: %u bytes", da_len);
    }
    level[0] = 0;
    level[1] = 0;
    lr = 0;

    for (uint32_t i = 0; i < len; i += byte_per_sample) {
        //ESP_LOGI(BT_AV_TAG, "1: %02x%02x%02x%02x%02x%02x%02x%02x", data[i], data[i + 1], data[i + 2], data[i + 3], data[i + 4], data[i + 5], data[i + 6], data[i + 7]);
        sample = 0;
        for (int8_t j = byte_per_sample - 1; j >= 0; j--) {
            sample = sample << 8;
            sample |= data[i + j];
        }

        //apply volume
        da_sample = (int32_t)i_volume * sample;
        level[lr] = da_sample < 0 ? MAX(level[lr], -da_sample) : MAX(level[lr], da_sample);
        for (uint8_t j = 0; j < byte_per_da_sample; j++) {
            da_data[2 * i + j] = (uint8_t)(da_sample & 0xff);
            da_sample = da_sample >> 8;
        }
        //ESP_LOGI(BT_AV_TAG, "2: %02x%02x%02x%02x%02x%02x%02x%02x", var_data[i], var_data[i + 1], var_data[i + 2], var_data[i + 3], var_data[i + 4], var_data[i + 5], var_data[i + 6], var_data[i + 7]);
        lr ^= 1;
    }
    update_vu_meter(level);

    write_ringbuf(da_data, len << 1);
    if (++s_pkt_cnt % 100 == 0) {
        ESP_LOGI(BT_AV_TAG, "Audio packet count %u", s_pkt_cnt);
        display_packets(s_pkt_cnt);
    }
}

void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param)
{
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);
    uint8_t *attr_text = (uint8_t *) malloc (rc->meta_rsp.attr_length + 1);
    memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
    attr_text[rc->meta_rsp.attr_length] = 0;

    rc->meta_rsp.attr_text = attr_text;
}

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
        bt_app_alloc_meta_buffer(param);
        /* fall through */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        bt_app_work_dispatch(bt_av_hdl_avrc_tg_evt, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);
        break;
    default:
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    esp_a2d_cb_param_t *a2d = NULL;
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(BT_AV_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
             s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            display_state("disconnected", NULL, 3);
            led_on(RED);
            bt_i2s_task_shut_down();
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED){
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

            get_remote_name(bda, &remote_name);
            display_state("connected to", remote_name, 3);
            led_on(ORANGE);

            bt_i2s_task_start_up();
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
        s_audio_state = a2d->audio_stat.state;
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
            s_pkt_cnt = 0;
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio stream configuration, codec type %d", a2d->audio_cfg.mcc.type);
        // for now only SBC stream is supported
        if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;
            char oct0 = a2d->audio_cfg.mcc.cie.sbc[0];
            if (oct0 & (0x01 << 6)) {
                sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                sample_rate = 48000;
            }
            i2s_set_clk(0, sample_rate, 32, 2);

            ESP_LOGI(BT_AV_TAG, "Configure audio player %x-%x-%x-%x",
                     a2d->audio_cfg.mcc.cie.sbc[0],
                     a2d->audio_cfg.mcc.cie.sbc[1],
                     a2d->audio_cfg.mcc.cie.sbc[2],
                     a2d->audio_cfg.mcc.cie.sbc[3]);
            ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate=%d", sample_rate);

            display_sample_rate(sample_rate);
        }
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

static void bt_av_new_track(void)
{
    // request metadata
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_GENRE;
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);

    // register notification if peer support the event_id
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_TRACK_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE, ESP_AVRC_RN_TRACK_CHANGE, 0);
    }
}

static void bt_av_playback_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    }
}

static void bt_av_play_pos_changed(void)
{
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_POS_CHANGED)) {
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE, ESP_AVRC_RN_PLAY_POS_CHANGED, 10);
    }
}

void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    switch (event_id) {
    case ESP_AVRC_RN_TRACK_CHANGE:
        bt_av_new_track();
        break;
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        ESP_LOGI(BT_AV_TAG, "Playback status changed: 0x%x", event_parameter->playback);
        bt_av_playback_changed();
        break;
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        ESP_LOGI(BT_AV_TAG, "Play position changed: %d-ms", event_parameter->play_pos);
        bt_av_play_pos_changed();
        break;
    }
}

static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s evt %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        if (rc->conn_stat.connected) {
            // get remote supported event_ids of peer AVRCP Target
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
        } else {
            // clear peer notification capability record
            s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d", rc->psth_rsp.key_code, rc->psth_rsp.key_state);
        break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
        free(rc->meta_rsp.attr_text);
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
        bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %x, TG features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;
        bt_av_new_track();
        bt_av_playback_changed();
        bt_av_play_pos_changed();
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}


static uint8_t vol_to_pct(uint8_t vol) {
    return (uint32_t)vol * 100 / 0x7f;
};


#define VOL_MIN         30.0
#define VOL_MAX         65536.0

/*
//static const float vol_log_min = 3.46573590279973;      // precalculated: log(vol_min);
//static const float vol_log_diff = 7.6246189861594;      // precalculated: log(vol_max) - log(vol_min);
static const float vol_log_min = log(VOL_MIN);
static const float vol_log_diff = log(VOL_MAX) - log(VOL_MIN);

//calculate volume with exp function
static uint32_t vol_calc_exp(uint8_t vol) {
    if (vol == 0) return 0;
    //if (vol == 0x7f) return VOL_MAX;

    return floor(exp(vol_log_min + vol_log_diff / 100 * vol_to_pct(vol)));
}
*/

#define VOL_POWER       3.0
static const float vol_pow_min = pow(VOL_MIN, (1.0 / VOL_POWER));
static const float vol_pow_diff = pow(VOL_MAX, (1.0 / VOL_POWER)) - pow(VOL_MIN, (1.0 / VOL_POWER));

//calculate volume with power function
static uint32_t vol_calc_pow(uint8_t vol) {
    if (vol == 0) return 0;
    if (vol == 0x7f) return VOL_MAX;

    //ESP_LOGI(BT_RC_TG_TAG, "vol_pow_min: %f  vol_pow_diff: %f  vol_to_pct: %u  VOL_POWER: %f", vol_pow_min, vol_pow_diff, vol_to_pct(vol), VOL_POWER);
    return floor(pow((vol_pow_min + vol_pow_diff / 100 * vol_to_pct(vol)), VOL_POWER));
}


static void volume_set_by_controller(uint8_t volume)
{
    _lock_acquire(&s_volume_lock);
    s_volume = volume;
    i_volume = vol_calc_pow(volume);
    _lock_release(&s_volume_lock);
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set by remote controller %d (%u) -> %d%%", volume, i_volume, vol_to_pct((int32_t)volume));

    display_volume(s_volume);
}

void volume_set_by_local_host(uint8_t volume)
{
    _lock_acquire(&s_volume_lock);
    s_volume = volume;
    i_volume = vol_calc_pow(volume);
    _lock_release(&s_volume_lock);
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set locally to: %d (%u) -> %d%%", volume, i_volume, vol_to_pct((int32_t)volume));

    if (s_volume_notify && ! s_volume_notify_disabled) {
        esp_avrc_rn_param_t rn_param;
        rn_param.volume = s_volume;
        if (esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn_param) != ESP_OK) {
            //disable notify at all
            s_volume_notify_disabled = true;
        }
        s_volume_notify = false;
    }

    display_volume(s_volume);
}

static uint8_t volume_last_muted = round(80.0 * 0x7f / 100);
void volume_mute() {
    volume_last_muted = s_volume;
    volume_set_by_local_host(0);
}
void volume_restore() {
    volume_set_by_local_host(volume_last_muted);
}

void volume_max() {
    volume_set_by_local_host(0x7f);
}

void volume_up(uint8_t vol_diff) {
    if (s_volume == 0x7f) return;
    uint8_t new_pct = MIN(100, vol_to_pct(s_volume) + vol_diff);
    uint8_t new_vol = s_volume;
    while (vol_to_pct(new_vol) != new_pct) new_vol++;
    volume_set_by_local_host(new_vol);
    //ESP_LOGI(BT_RC_TG_TAG, "modify volume: old: %02x  %d%%  change: %d%%  new: %02x", s_volume, vol_to_pct(s_volume), change, pct_to_vol((int32_t)vol_to_pct(s_volume) + (int32_t)change));
}
void volume_down(uint8_t vol_diff) {
    if (s_volume == 0) return;
    if (s_volume <= 2) {
        volume_set_by_local_host(0);
        return;
    }
    uint8_t new_pct = vol_to_pct(s_volume);
    if (new_pct <= vol_diff) {
        new_pct = 0;
    }
    else {
        new_pct -= vol_diff;
    }
    uint8_t new_vol = s_volume;
    while (vol_to_pct(new_vol) != new_pct) new_vol--;
    volume_set_by_local_host(new_vol);
}

static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_RC_TG_TAG, "%s evt %d", __func__, event);
    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(p_param);
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_TG_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        break;
    }
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
        ESP_LOGI(BT_RC_TG_TAG, "AVRC passthrough cmd: key_code 0x%x, key_state %d", rc->psth_cmd.key_code, rc->psth_cmd.key_state);
        break;
    }
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        ESP_LOGI(BT_RC_TG_TAG, "AVRC set absolute volume: %d%%", vol_to_pct((int32_t)rc->set_abs_vol.volume * 100 / 0x7f));
        volume_set_by_controller(rc->set_abs_vol.volume);
        break;
    }
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
        ESP_LOGI(BT_RC_TG_TAG, "AVRC register event notification: %d, param: 0x%x", rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_volume_notify = true;
            esp_avrc_rn_param_t rn_param;
            rn_param.volume = s_volume;
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
        }
        break;
    }
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT: {
        ESP_LOGI(BT_RC_TG_TAG, "AVRC remote features %x, CT features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.ct_feat_flag);
        break;
    }
    default:
        ESP_LOGE(BT_RC_TG_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}
