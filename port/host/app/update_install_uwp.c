/* host/app/update_install.h on UWP: there is no installer to run (the package is updated by
 * installing a newer one), so the update check still tells of a new release but never offers to
 * install it (updates.c asks khdays_update_install_possible first). */
#include "update_install.h"

int khdays_update_install_possible(void)
{
    return 0;
}

int khdays_update_install_folder(char *out, size_t size)
{
    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
    return 0;
}

void khdays_update_install_clean(void)
{
}

int khdays_update_install_run(const char *file, char *why, size_t size)
{
    static const char reason[] = "UWP packages are updated by installing a newer package";
    size_t i;
    (void)file;
    if (why != NULL && size > 0) {
        for (i = 0; i + 1 < size && i < sizeof(reason) - 1; ++i) {
            why[i] = reason[i];
        }
        why[i] = '\0';
    }
    return 0;
}
