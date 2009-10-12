/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define LOG_TAG "Vold"

#include "cutils/log.h"

#include "VolumeManager.h"
#include "CommandListener.h"
#include "NetlinkManager.h"
#include "DeviceVolume.h"

static int process_config(VolumeManager *vm);

int main() {

    VolumeManager *vm;
    CommandListener *cl;
    NetlinkManager *nm;

    LOGI("Vold 2.0 firing up");

    /* Create our singleton managers */
    if (!(vm = VolumeManager::Instance())) {
        LOGE("Unable to create VolumeManager");
        exit(1);
    };

    if (!(nm = NetlinkManager::Instance())) {
        LOGE("Unable to create NetlinkManager");
        exit(1);
    };

    cl = new CommandListener();
    vm->setBroadcaster((SocketListener *) cl);
    nm->setBroadcaster((SocketListener *) cl);

    if (vm->start()) {
        LOGE("Unable to start VolumeManager (%s)", strerror(errno));
        exit(1);
    }

    if (process_config(vm)) {
        LOGE("Error reading configuration (%s)", strerror(errno));
        exit(1);
    }

    if (nm->start()) {
        LOGE("Unable to start NetlinkManager (%s)", strerror(errno));
        exit(1);
    }

    /*
     * Now that we're up, we can respond to commands
     */
    if (cl->startListener()) {
        LOGE("Unable to start CommandListener (%s)", strerror(errno));
        exit(1);
    }

    // Eventually we'll become the monitoring thread
    while(1) {
        sleep(1000);
    }

    LOGI("Vold exiting");
    exit(0);
}

static int process_config(VolumeManager *vm) {
    FILE *fp;
    int n = 0;
    char line[255];

    if (!(fp = fopen("/etc/vold.fstab", "r"))) {
        return -1;
    }

    while(fgets(line, sizeof(line), fp)) {
        char *next = line;
        char *type, *label, *mount_point;

        n++;
        line[strlen(line)-1] = '\0';

        if (line[0] == '#' || line[0] == '\0')
            continue;

        if (!(type = strsep(&next, " \t"))) {
            LOGE("Error parsing type");
            goto out_syntax;
        }
        if (!(label = strsep(&next, " \t"))) {
            LOGE("Error parsing label");
            goto out_syntax;
        }
        if (!(mount_point = strsep(&next, " \t"))) {
            LOGE("Error parsing mount point");
            goto out_syntax;
        }

        if (!strcmp(type, "dev_mount")) {
            DeviceVolume *dv = NULL;
            char *part, *sysfs_path;

            if (!(part = strsep(&next, " \t"))) {
                LOGE("Error parsing partition");
                goto out_syntax;
            }
            if (strcmp(part, "auto") && atoi(part) == 0) {
                LOGE("Partition must either be 'auto' or 1 based index instead of '%s'", part);
                goto out_syntax;
            }

            dv = new DeviceVolume(label, mount_point, atoi(part));

            while((sysfs_path = strsep(&next, " \t"))) {
                if (dv->addPath(sysfs_path)) {
                    LOGE("Failed to add devpath %s to volume %s", sysfs_path,
                         label);
                    goto out_fail;
                }
            }
            vm->addVolume(dv);
        } else if (!strcmp(type, "map_mount")) {
        } else {
            LOGE("Unknown type '%s'", type);
            goto out_syntax;
        }
    }

    fclose(fp);
    return 0;

out_syntax:
    LOGE("Syntax error on config line %d", n);
    errno = -EINVAL;
out_fail:
    fclose(fp);
    return -1;   
}