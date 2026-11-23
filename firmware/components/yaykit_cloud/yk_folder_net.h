// yk_folder_net.c, inside yaykit_cloud: the shared folders' grants at the device service, handed to yaykit_tf.
#pragma once

/** Hands the calls to yaykit_tf (yk_folder_net_set) when the firmware links it; yk_account_init calls it. */
void yk_folder_net_offer(void);
/** This device's records of the shared folders' answers go (yaykit_tf's yk_folder_records_forget, when the firmware
 *  links it): the account let the device go, and a new owner's service has none of them. yk_account's forget() calls
 *  it. */
void yk_folder_net_forget(void);
/** The priority of the task that makes the shared folders' TLS (Yay 0.2 F6 第 4 条): 3 unless set, as before (every
 *  firmware that does not call this is as it was), and never above 3 (0..3: no caller lifts the task). Yay sets its
 *  YAY_BG_NET_PRIORITY (0, IDLE's own): a handshake beside a held key starved IDLE1 into task_wdt. Taken by the next
 *  task it makes, from any task. Inside yaykit_cloud, not in its include/: apps are blocked from yk_folder_net_* (review
 *  rules), so Yay declares it itself, as it does yk_folder_net_dev_slow. */
void yk_folder_net_set_priority(int priority);
