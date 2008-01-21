/*
 * Copyright (c) 2008 Andrew G. Morgan <morgan@kernel.org>
 *
 * This is a simple 'bash' wrapper program that can be used to
 * raise and lower both the bset and pI capabilities before invoking
 * /bin/bash (hardcoded right now).
 *
 * The --print option can be used as a quick test whether various
 * capability manipulations work as expected (or not).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <unistd.h>

/* prctl based API for altering character of current process */
#define PR_GET_KEEPCAPS    7
#define PR_SET_KEEPCAPS    8
#define PR_CAPBSET_READ   23
#define PR_CAPBSET_DROP   24

static const cap_value_t raise_setpcap[1] = { CAP_SETPCAP };
static const cap_value_t raise_chroot[1] = { CAP_SYS_CHROOT };

int main(int argc, char *argv[], char *envp[])
{
    unsigned i;

    for (i=1; i<argc; ++i) {
	if (!memcmp("--drop=", argv[i], 4)) {
	    char *ptr;
	    cap_t orig, raised_for_setpcap;

	    /*
	     * We need to do this here because --inh=XXX may have reset
	     * orig and it isn't until we are within the --drop code that
	     * we know what the prevailing (orig) pI value is.
	     */
	    orig = cap_get_proc();
	    if (orig == NULL) {
		perror("Capabilities not available");
		exit(1);
	    }

	    raised_for_setpcap = cap_dup(orig);
	    if (raised_for_setpcap == NULL) {
		fprintf(stderr, "BSET modification requires CAP_SETPCAP\n");
		exit(1);
	    }

	    if (cap_set_flag(raised_for_setpcap, CAP_EFFECTIVE, 1,
			     raise_setpcap, CAP_SET) != 0) {
		perror("unable to select CAP_SETPCAP");
		exit(1);
	    }

	    for (ptr = argv[i]+7; (ptr = strtok(ptr, ",")); ptr = NULL) {
		/* find name for token */
		cap_value_t cap;
		int status;

		if (cap_from_name(ptr, &cap) != 0) {
		    fprintf(stderr, "capability [%s] is unknown to libcap\n",
			    ptr);
		    exit(1);
		}
		if (cap_set_proc(raised_for_setpcap) != 0) {
		    perror("unable to raise CAP_SETPCAP for BSET changes");
		    exit(1);
		}
		status = prctl(PR_CAPBSET_DROP, cap);
		if (cap_set_proc(orig) != 0) {
		    perror("unable to lower CAP_SETPCAP post BSET change");
		    exit(1);
		}
		if (status) {
		    fprintf(stderr, "failed to drop [%s=%u]\n", ptr, cap);
		    exit(1);
		}
	    }

	    cap_free(raised_for_setpcap);
	    cap_free(orig);
	} else if (!memcmp("--inh=", argv[i], 4)) {
	    cap_t all, raised_for_setpcap;
	    char *text;
	    char *ptr;

	    all = cap_get_proc();
	    if (all == NULL) {
		perror("Capabilities not available");
		exit(1);
	    }

	    raised_for_setpcap = cap_dup(all);
	    if ((raised_for_setpcap != NULL)
		|| (cap_set_flag(raised_for_setpcap, CAP_EFFECTIVE, 1,
				 raise_setpcap, CAP_SET) != 0)) {
		cap_free(raised_for_setpcap);
		raised_for_setpcap = NULL;
	    }

	    text = cap_to_text(all, NULL);
	    if (text == NULL) {
		perror("Fatal error concerning process capabilities");
		exit(1);
	    }
	    ptr = malloc(4 + strlen(argv[i]+6) + strlen(text));
	    if (ptr == NULL) {
		perror("Out of memory for inh set\n");
		exit(1);
	    }
	    sprintf(ptr, "%s all-i %s+i", text, argv[i]+6);
	    cap_free(all);

	    all = cap_from_text(ptr);
	    if (all == NULL) {
		perror("Fatal error internalizing capabilities");
		exit(1);
	    }
	    cap_free(text);
	    free(ptr);

	    if (raised_for_setpcap != NULL) {
		/*
		 * This is only for the case that pP does not contain
		 * the requested change to pI.. Failing here is not
		 * indicative of the cap_set_proc(all) failing (always).
		 */
		(void) cap_set_proc(raised_for_setpcap);
		cap_free(raised_for_setpcap);
		raised_for_setpcap = NULL;
	    }

	    if (cap_set_proc(all) != 0) {
		perror("Unable to set inheritable capabilities");
		exit(1);
	    }
	    /*
	     * Since status is based on orig, we don't want to restore
	     * the previous value of 'all' again here!
	     */

	    cap_free(all);
	} else if (!memcmp("--chroot=", argv[i], 9)) {
	    int status;
	    cap_t orig, raised_for_chroot;

	    orig = cap_get_proc();
	    if (orig == NULL) {
		perror("Capabilities not available.");
		exit(1);
	    }

	    raised_for_chroot = cap_dup(orig);
	    if (raised_for_chroot == NULL) {
		perror("Unable to duplicate capabilities");
		exit(1);
	    }

	    if (cap_set_flag(raised_for_chroot, CAP_EFFECTIVE, 1, raise_chroot,
			     CAP_SET) != 0) {
		perror("unable to select CAP_SET_SYS_CHROOT");
		exit(1);
	    }

	    if (cap_set_proc(raised_for_chroot) != 0) {
		perror("unable to raise CAP_SYS_CHROOT");
		exit(1);
	    }
	    cap_free(raised_for_chroot);

	    status = chroot(argv[i]+9);
	    if (cap_set_proc(orig) != 0) {
		perror("unable to lower CAP_SYS_CHROOT");
		exit(1);
	    }
	    cap_free(orig);

	    if (status != 0) {
		fprintf(stderr, "Unable to chroot to [%s]", argv[i]+9);
		exit(1);
	    }
	} else if (!strcmp("--print", argv[i])) {
	    unsigned cap;
	    int set;
	    cap_t all;
	    char *text;
	    const char *sep;

	    all = cap_get_proc();
	    text = cap_to_text(all, NULL);
	    printf("Current: %s\n", text);
	    cap_free(text);
	    cap_free(all);

	    printf("Bounding set =");
 	    sep = "";
	    for (cap=0; (set = prctl(PR_CAPBSET_READ, cap)) != -1; cap++) {
		const char *ptr;
		if (!set) {
		    continue;
		}

		ptr = cap_to_name(cap);
		if (ptr == 0) {
		    printf("%s%u", sep, cap);
		} else {
		    printf("%s%s", sep, ptr);
		}
		sep = ",";
	    }
	    printf("\n");
	} else if (!strcmp("--", argv[i])) {
	    argv[i] = strdup("/bin/bash");
	    argv[argc] = NULL;
	    execve("/bin/bash", argv+i, envp);
	    fprintf(stderr, "execve /bin/bash failed!\n");
	    exit(1);
	} else {
	    printf("usage: %s [args ...]\n"
		   "  --help         this message\n"
		   "  --print        display capability relevant state\n"
		   "  --drop=xxx     remove xxx,.. capabilities from bset\n"
		   "  --inh=xxx      set xxx,.. inheritiable set\n"
		   "  --chroot=path  chroot(2) to this path to invoke bash\n"
		   "  --             remaing arguments are for /bin/bash\n"
		   "                 (without -- [%s] will simply exit(0))\n",
		   argv[0], argv[0]);

	    exit(strcmp("--help", argv[i]) != 0);
	}
    }

    exit(0);
}