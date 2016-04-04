/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, The CyanogenMod Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_NIDEBUG 0

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdlib.h>

#define LOG_TAG "QCOM PowerHAL"
#include <utils/Log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>
#include <pthread.h>

#include "utils.h"
#include "metadata-defs.h"
#include "hint-data.h"
#include "performance.h"
#include "power-common.h"

#define PLATFORM_SLEEP_MODES 2
#define XO_VOTERS 3
#define VMIN_VOTERS 0

#define RPM_PARAMETERS 4
#define NUM_PARAMETERS 10

#ifndef RPM_STAT
#define RPM_STAT "/d/rpm_stats"
#endif

#ifndef RPM_MASTER_STAT
#define RPM_MASTER_STAT "/d/rpm_master_stats"
#endif

/* RPM runs at 19.2Mhz. Divide by 19200 for msec */
#define RPM_CLK 19200

const char *parameter_names[] = {
    "vlow_count",
    "accumulated_vlow_time",
    "vmin_count",
    "accumulated_vmin_time",
    "xo_accumulated_duration",
    "xo_count",
    "xo_accumulated_duration",
    "xo_count",
    "xo_accumulated_duration",
    "xo_count"
};

static int saved_dcvs_cpu0_slack_max = -1;
static int saved_dcvs_cpu0_slack_min = -1;
static int saved_mpdecision_slack_max = -1;
static int saved_mpdecision_slack_min = -1;
static int slack_node_rw_failed = 0;
static int display_hint_sent;

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

static pthread_mutex_t hint_mutex = PTHREAD_MUTEX_INITIALIZER;

static void power_init(__attribute__((unused))struct power_module *module)
{
    ALOGI("QCOM power HAL initing.");
}

static void process_video_decode_hint(void *metadata)
{
    char governor[80];
    struct video_decode_metadata_t video_decode_metadata;

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");

        return;
    }

    if (metadata) {
        ALOGI("Processing video decode hint. Metadata: %s", (char *)metadata);
    }

    /* Initialize encode metadata struct fields. */
    memset(&video_decode_metadata, 0, sizeof(struct video_decode_metadata_t));
    video_decode_metadata.state = -1;
    video_decode_metadata.hint_id = DEFAULT_VIDEO_DECODE_HINT_ID;

    if (metadata) {
        if (parse_video_decode_metadata((char *)metadata, &video_decode_metadata) ==
            -1) {
            ALOGE("Error occurred while parsing metadata.");
            return;
        }
    } else {
        return;
    }

    if (video_decode_metadata.state == 1) {
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            int resource_values[] = {THREAD_MIGRATION_SYNC_OFF};

            perform_hint_action(video_decode_metadata.hint_id,
                    resource_values, ARRAY_SIZE(resource_values));
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            int resource_values[] = {TR_MS_30, HISPEED_LOAD_90, HS_FREQ_1026, THREAD_MIGRATION_SYNC_OFF};

            perform_hint_action(video_decode_metadata.hint_id,
                    resource_values, ARRAY_SIZE(resource_values));
        }
    } else if (video_decode_metadata.state == 0) {
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            undo_hint_action(video_decode_metadata.hint_id);
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            undo_hint_action(video_decode_metadata.hint_id);
        }
    }
}

static void process_video_encode_hint(void *metadata)
{
    char governor[80];
    struct video_encode_metadata_t video_encode_metadata;

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");

        return;
    }

    /* Initialize encode metadata struct fields. */
    memset(&video_encode_metadata, 0, sizeof(struct video_encode_metadata_t));
    video_encode_metadata.state = -1;
    video_encode_metadata.hint_id = DEFAULT_VIDEO_ENCODE_HINT_ID;

    if (metadata) {
        if (parse_video_encode_metadata((char *)metadata, &video_encode_metadata) ==
            -1) {
            ALOGE("Error occurred while parsing metadata.");
            return;
        }
    } else {
        return;
    }

    if (video_encode_metadata.state == 1) {
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            int resource_values[] = {IO_BUSY_OFF, SAMPLING_DOWN_FACTOR_1, THREAD_MIGRATION_SYNC_OFF};

            perform_hint_action(video_encode_metadata.hint_id,
                resource_values, ARRAY_SIZE(resource_values));
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            int resource_values[] = {TR_MS_30, HISPEED_LOAD_90, HS_FREQ_1026, THREAD_MIGRATION_SYNC_OFF,
                INTERACTIVE_IO_BUSY_OFF};

            perform_hint_action(video_encode_metadata.hint_id,
                    resource_values, ARRAY_SIZE(resource_values));
        }
    } else if (video_encode_metadata.state == 0) {
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            undo_hint_action(video_encode_metadata.hint_id);
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            undo_hint_action(video_encode_metadata.hint_id);
        }
    }
}

int __attribute__ ((weak)) power_hint_override(
        __attribute__((unused)) struct power_module *module,
        __attribute__((unused)) power_hint_t hint,
        __attribute__((unused)) void *data)
{
    return HINT_NONE;
}

extern void interaction(int duration, int num_args, int opt_list[]);

static void power_hint(__attribute__((unused)) struct power_module *module, power_hint_t hint,
        void *data)
{
    pthread_mutex_lock(&hint_mutex);

    /* Check if this hint has been overridden. */
    if (power_hint_override(module, hint, data) == HINT_HANDLED) {
        /* The power_hint has been handled. We can skip the rest. */
        goto out;
    }

    switch(hint) {
        case POWER_HINT_VSYNC:
        case POWER_HINT_INTERACTION:
        case POWER_HINT_CPU_BOOST:
        case POWER_HINT_LAUNCH_BOOST:
        case POWER_HINT_AUDIO:
        case POWER_HINT_SET_PROFILE:
        case POWER_HINT_LOW_POWER:
        break;
        case POWER_HINT_VIDEO_ENCODE:
            process_video_encode_hint(data);
        break;
        case POWER_HINT_VIDEO_DECODE:
            process_video_decode_hint(data);
        break;
        default:
        break;
    }

out:
    pthread_mutex_unlock(&hint_mutex);
}

int __attribute__ ((weak)) set_interactive_override(
        __attribute__((unused)) struct power_module *module,
        __attribute__((unused)) int on)
{
    return HINT_NONE;
}

int __attribute__ ((weak)) get_number_of_profiles()
{
    return 0;
}

#ifdef SET_INTERACTIVE_EXT
extern void cm_power_set_interactive_ext(int on);
#endif

void set_interactive(struct power_module *module, int on)
{
    char governor[80];
    char tmp_str[NODE_MAX];
    struct video_encode_metadata_t video_encode_metadata;
    int rc = 0;

    pthread_mutex_lock(&hint_mutex);

    /**
     * Ignore consecutive display-off hints
     * Consecutive display-on hints are already handled
     */
    if (display_hint_sent && !on)
        goto out;

    display_hint_sent = !on;

#ifdef SET_INTERACTIVE_EXT
    cm_power_set_interactive_ext(on);
#endif

    if (set_interactive_override(module, on) == HINT_HANDLED) {
        goto out;
    }

    ALOGI("Got set_interactive hint");

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");
        goto out;
    }

    if (!on) {
        /* Display off. */
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            int resource_values[] = { MS_500, THREAD_MIGRATION_SYNC_OFF };

            perform_hint_action(DISPLAY_STATE_HINT_ID,
                    resource_values, ARRAY_SIZE(resource_values));
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            int resource_values[] = {TR_MS_50, THREAD_MIGRATION_SYNC_OFF};

            perform_hint_action(DISPLAY_STATE_HINT_ID,
                    resource_values, ARRAY_SIZE(resource_values));
        } else if ((strncmp(governor, MSMDCVS_GOVERNOR, strlen(MSMDCVS_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(MSMDCVS_GOVERNOR))) {
            /* Display turned off. */
            if (sysfs_read(DCVS_CPU0_SLACK_MAX_NODE, tmp_str, NODE_MAX - 1)) {
                if (!slack_node_rw_failed) {
                    ALOGE("Failed to read from %s", DCVS_CPU0_SLACK_MAX_NODE);
                }

                rc = 1;
            } else {
                saved_dcvs_cpu0_slack_max = atoi(tmp_str);
            }

            if (sysfs_read(DCVS_CPU0_SLACK_MIN_NODE, tmp_str, NODE_MAX - 1)) {
                if (!slack_node_rw_failed) {
                    ALOGE("Failed to read from %s", DCVS_CPU0_SLACK_MIN_NODE);
                }

                rc = 1;
            } else {
                saved_dcvs_cpu0_slack_min = atoi(tmp_str);
            }

            if (sysfs_read(MPDECISION_SLACK_MAX_NODE, tmp_str, NODE_MAX - 1)) {
                if (!slack_node_rw_failed) {
                    ALOGE("Failed to read from %s", MPDECISION_SLACK_MAX_NODE);
                }

                rc = 1;
            } else {
                saved_mpdecision_slack_max = atoi(tmp_str);
            }

            if (sysfs_read(MPDECISION_SLACK_MIN_NODE, tmp_str, NODE_MAX - 1)) {
                if(!slack_node_rw_failed) {
                    ALOGE("Failed to read from %s", MPDECISION_SLACK_MIN_NODE);
                }

                rc = 1;
            } else {
                saved_mpdecision_slack_min = atoi(tmp_str);
            }

            /* Write new values. */
            if (saved_dcvs_cpu0_slack_max != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", 10 * saved_dcvs_cpu0_slack_max);

                if (sysfs_write(DCVS_CPU0_SLACK_MAX_NODE, tmp_str) != 0) {
                    if (!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", DCVS_CPU0_SLACK_MAX_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_dcvs_cpu0_slack_min != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", 10 * saved_dcvs_cpu0_slack_min);

                if (sysfs_write(DCVS_CPU0_SLACK_MIN_NODE, tmp_str) != 0) {
                    if(!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", DCVS_CPU0_SLACK_MIN_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_mpdecision_slack_max != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", 10 * saved_mpdecision_slack_max);

                if (sysfs_write(MPDECISION_SLACK_MAX_NODE, tmp_str) != 0) {
                    if(!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", MPDECISION_SLACK_MAX_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_mpdecision_slack_min != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", 10 * saved_mpdecision_slack_min);

                if (sysfs_write(MPDECISION_SLACK_MIN_NODE, tmp_str) != 0) {
                    if(!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", MPDECISION_SLACK_MIN_NODE);
                    }

                    rc = 1;
                }
            }

            slack_node_rw_failed = rc;
        }
    } else {
        /* Display on. */
        if ((strncmp(governor, ONDEMAND_GOVERNOR, strlen(ONDEMAND_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(ONDEMAND_GOVERNOR))) {
            undo_hint_action(DISPLAY_STATE_HINT_ID);
        } else if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            undo_hint_action(DISPLAY_STATE_HINT_ID);
        } else if ((strncmp(governor, MSMDCVS_GOVERNOR, strlen(MSMDCVS_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(MSMDCVS_GOVERNOR))) {
            /* Display turned on. Restore if possible. */
            if (saved_dcvs_cpu0_slack_max != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", saved_dcvs_cpu0_slack_max);

                if (sysfs_write(DCVS_CPU0_SLACK_MAX_NODE, tmp_str) != 0) {
                    if (!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", DCVS_CPU0_SLACK_MAX_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_dcvs_cpu0_slack_min != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", saved_dcvs_cpu0_slack_min);

                if (sysfs_write(DCVS_CPU0_SLACK_MIN_NODE, tmp_str) != 0) {
                    if (!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", DCVS_CPU0_SLACK_MIN_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_mpdecision_slack_max != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", saved_mpdecision_slack_max);

                if (sysfs_write(MPDECISION_SLACK_MAX_NODE, tmp_str) != 0) {
                    if (!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", MPDECISION_SLACK_MAX_NODE);
                    }

                    rc = 1;
                }
            }

            if (saved_mpdecision_slack_min != -1) {
                snprintf(tmp_str, NODE_MAX, "%d", saved_mpdecision_slack_min);

                if (sysfs_write(MPDECISION_SLACK_MIN_NODE, tmp_str) != 0) {
                    if (!slack_node_rw_failed) {
                        ALOGE("Failed to write to %s", MPDECISION_SLACK_MIN_NODE);
                    }

                    rc = 1;
                }
            }

            slack_node_rw_failed = rc;
        }
    }

out:
    pthread_mutex_unlock(&hint_mutex);
}

void set_feature(struct power_module *module __unused, feature_t feature, int state)
{
#ifdef TAP_TO_WAKE_NODE
    char tmp_str[NODE_MAX];
    if (feature == POWER_FEATURE_DOUBLE_TAP_TO_WAKE) {
        snprintf(tmp_str, NODE_MAX, "%d", state);
        sysfs_write(TAP_TO_WAKE_NODE, tmp_str);
        return;
    }
#endif
}

int get_feature(struct power_module *module __unused, feature_t feature)
{
    if (feature == POWER_FEATURE_SUPPORTED_PROFILES) {
        return get_number_of_profiles();
    }
    return -1;
}

static ssize_t get_number_of_platform_modes(struct power_module *module __unused) {
   return PLATFORM_SLEEP_MODES;
}

static int get_voter_list(struct power_module *module __unused, size_t *voter) {
   voter[0] = XO_VOTERS;
   voter[1] = VMIN_VOTERS;

   return 0;
}

static int extract_stats(uint64_t *list, char *file,
    unsigned int num_parameters, unsigned int index) {
    FILE *fp;
    ssize_t read;
    size_t len;
    char *line;
    int ret;

    fp = fopen(file, "r");
    if (fp == NULL) {
        ret = -errno;
        ALOGE("%s: failed to open: %s", __func__, strerror(errno));
        return ret;
    }

    for (line = NULL, len = 0;
         ((read = getline(&line, &len, fp) != -1) && (index < num_parameters));
         free(line), line = NULL, len = 0) {
        uint64_t value;
        char* offset;

        size_t begin = strspn(line, " \t");
        if (strncmp(line + begin, parameter_names[index], strlen(parameter_names[index]))) {
            continue;
        }

        offset = memchr(line, ':', len);
        if (!offset) {
            continue;
        }

        if (!strcmp(file, RPM_MASTER_STAT)) {
            /* RPM_MASTER_STAT is reported in hex */
            sscanf(offset, ":%" SCNx64, &value);
            /* Duration is reported in rpm SLEEP TICKS */
            if (!strcmp(parameter_names[index], "xo_accumulated_duration")) {
                value /= RPM_CLK;
            }
        } else {
            /* RPM_STAT is reported in decimal */
            sscanf(offset, ":%" SCNu64, &value);
        }
        list[index] = value;
        index++;
    }
    free(line);

    fclose(fp);
    return 0;
}

static int get_platform_low_power_stats(struct power_module *module __unused,
    power_state_platform_sleep_state_t *list) {
    uint64_t stats[sizeof(parameter_names)] = {0};
    int ret;

    if (!list) {
        return -EINVAL;
    }

    ret = extract_stats(stats, RPM_STAT, RPM_PARAMETERS, 0);

    if (ret) {
        return ret;
    }

    ret = extract_stats(stats, RPM_MASTER_STAT, NUM_PARAMETERS, 4);

    if (ret) {
        return ret;
    }

    /* Update statistics for XO_shutdown */
    strcpy(list[0].name, "XO_shutdown");
    list[0].total_transitions = stats[0];
    list[0].residency_in_msec_since_boot = stats[1];
    list[0].supported_only_in_suspend = false;
    list[0].number_of_voters = XO_VOTERS;

    /* Update statistics for APSS voter */
    strcpy(list[0].voters[0].name, "APSS");
    list[0].voters[0].total_time_in_msec_voted_for_since_boot = stats[4];
    list[0].voters[0].total_number_of_times_voted_since_boot = stats[5];

    /* Update statistics for MPSS voter */
    strcpy(list[0].voters[1].name, "MPSS");
    list[0].voters[1].total_time_in_msec_voted_for_since_boot = stats[6];
    list[0].voters[1].total_number_of_times_voted_since_boot = stats[7];

    /* Update statistics for LPASS voter */
    strcpy(list[0].voters[2].name, "PRONTO");
    list[0].voters[2].total_time_in_msec_voted_for_since_boot = stats[8];
    list[0].voters[2].total_number_of_times_voted_since_boot = stats[9];

    /* Update statistics for VMIN state */
    strcpy(list[1].name, "VMIN");
    list[1].total_transitions = stats[2];
    list[1].residency_in_msec_since_boot = stats[3];
    list[1].supported_only_in_suspend = false;
    list[1].number_of_voters = VMIN_VOTERS;

    return 0;
}

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_5,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "QCOM Power HAL",
        .author = "Qualcomm/CyanogenMod",
        .methods = &power_module_methods,
    },

    .init = power_init,
    .powerHint = power_hint,
    .setInteractive = set_interactive,
    .setFeature = set_feature,
    .getFeature = get_feature,
    .get_number_of_platform_modes = get_number_of_platform_modes,
    .get_platform_low_power_stats = get_platform_low_power_stats,
    .get_voter_list = get_voter_list
};
