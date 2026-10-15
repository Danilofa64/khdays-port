/* The commit this server is built from, said when it starts:
 * git archive writes it here (.gitattributes: export-subst), so a server
 * built from the sources the maintainer is given says it; the port's own
 * build passes it as KHDAYS_SERVER_COMMIT. */
#ifndef KHDAYS_SERVER_VERSION_H
#define KHDAYS_SERVER_VERSION_H

#ifdef KHDAYS_SERVER_COMMIT
#define KHDAYS_SERVER_VERSION KHDAYS_SERVER_COMMIT
#else
#define KHDAYS_SERVER_VERSION "$Format:%h$"
#endif

#endif
