// yk_folder_net.c, inside yaykit_cloud: the shared folders' grants at the device service, handed to yaykit_tf.
#pragma once

/** Hands the calls to yaykit_tf (yk_folder_net_set) when the firmware links it; yk_account_init calls it. */
void yk_folder_net_offer(void);
/** This device's records of the shared folders' answers go (yaykit_tf's yk_folder_records_forget, when the firmware
 *  links it): the account let the device go, and a new owner's service has none of them. yk_account's forget() calls
 *  it. */
void yk_folder_net_forget(void);
