// yk_session_url() (yaykit_session.h): the production session by the device's region. Apart from yaykit_session.c so
// that it is host tested with yaykit_cloud's yk_region.c alone (yaykit_cloud/test/test_region.c).
#include "sdkconfig.h"
#include "yaykit_session.h"
#include "yk_region.h"
#if CONFIG_YAY_DEV && CONFIG_YK_REGIONS
#include "yk_region_test.h"  // yaykit_cloud's own (CMakeLists.txt's PRIV_INCLUDE_DIRS): a test deployment for a row
#endif

const char *yk_session_url(void) {
#if CONFIG_YK_REGIONS
    yk_region_row_t row;
    const int at = yk_region_current();
#if CONFIG_YAY_DEV
    const char *test = yk_region_test_session(at);  // `region set`: that deployment's session, on its device host
    if (test) return test;
#endif
    if (at != YK_REGION_GLOBAL && yk_region_row(at, &row)) return row.session_url;
#endif
    return YK_SESSION_URL;
}
