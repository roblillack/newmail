/* newmail, version 1.0                ©2006 Robert Lillack, burningsoda.com */

#include <dirent.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_NAMLEN
#define NAMLEN(x) x->d_namlen
#else
#define NAMLEN(x) strlen(x->d_name)
#endif

struct dirnode {
  char *name;
  int unreadmails;
};

int compare_dirnodes(const void *n1, const void *n2) {
  return strcmp((*(struct dirnode**)n1)->name, (*(struct dirnode**)n2)->name);
}

int is_maildir(const char *pathname) {
  DIR *dir;
  struct dirent *e;
  struct stat sb;
  int found = 0;

  dir = opendir(pathname);
  if (!dir) return 0;

  while ((e = readdir(dir)) != NULL) {
    if (NAMLEN(e) == 3 && 
        ((strncmp(e->d_name, "cur", 3) == 0) ||
         (strncmp(e->d_name, "new", 3) == 0) ||
         (strncmp(e->d_name, "tmp", 3) == 0))) {
      char subdir[MAXPATHLEN + 1];
      snprintf(subdir, sizeof(subdir), "%s/%s", pathname, e->d_name);
      if ((stat(subdir, &sb) == 0) && S_ISDIR(sb.st_mode)) found++;
    }
  }
  closedir(dir);

  if (found == 3) return 1;
  else return 0;
}

int get_file_count(char *pathname, int checkflags) {
  DIR *dir;
  struct dirent *entry;
  int found = 0;
  char *comma;

  dir = opendir(pathname);
  if (!dir) return -1;

  while ((entry = readdir(dir)) != NULL) {
    /* since we don't want to stat() here, we loose
     * the ability to check for d_type == DT_DIR
     * to stay ANSI conform. */
    if (NAMLEN(entry) < 10) continue;
    if (checkflags) {
      /* make sure the SEEN flag is not there */
      comma = rindex(entry->d_name, ',');
      if (comma && strchr(comma, 'S') == NULL)
        found++;
    } else {
      found++;
    }
  }
  closedir(dir);

  return found;
}


void error_quit(char *message) {
  if (message) {
    fprintf(stderr, "%s\n", message);
  } else {
    fprintf(stderr, "Unable to allocate more memory. Exiting...\n");
  }
  exit(1);
}

int main(int argc, char **argv) {
  DIR* maildir;
  struct dirent *entry;
  struct dirnode *dp = NULL;
  struct dirnode **dirlist = NULL;
  int dirlist_count = 0;

  char maildirname[MAXPATHLEN + 1] = "";

  int showonlynew = 0;
  int muttmode = 0;
  int showtotal = 0;
  int showall = 0;
  int i, j;

  static struct option long_options[] = {
    {"all", 0, 0, 'a'},
    {"mutt", 0, 0, 'm'},
    {"total", 0, 0, 't'},
    {"no-unseen", 0, 0, 'n'},
    {0, 0, 0, 0}
  };

  while ((i = getopt_long(argc, argv, "amnt", long_options, NULL)) != -1) {
    switch (i) {
      case 'a': showall = 1; break;
      case 'm': muttmode = 1; break;
      case 'n': showonlynew = 1; break;
      case 't': showtotal = 1; break;
      default: break;
    }
  }

  if (optind < argc) {
    if (optind + 1 < argc) {
      fprintf(stderr, "%s: Only one directory structure allowed. "
          "Using: %s\n", argv[0], argv[optind]);
    }
    snprintf(maildirname, sizeof(maildirname), argv[optind]);
  } else {
    if (!getenv("HOME")) error_quit("Unable to determine $HOME directory");
    snprintf(maildirname, sizeof(maildirname), "%s/Maildir", getenv("HOME"));
  }

  if (!is_maildir(maildirname) && !muttmode) {
    fprintf(stderr, "warning: %s itself is not a Maildir."
                    " Continuing anyway...\n",
            maildirname);
  }

  maildir = opendir(maildirname);
  if (!maildir) {
    fprintf(stderr, "Directory %s does not exist.\n", maildirname);
    return -1;
  }

  /* step through the directory and find subdirs which are Maildirs */
  while ((entry = readdir(maildir)) != NULL) {
    if (!entry->d_name) continue;
    if (NAMLEN(entry) == 2 && strncmp(entry->d_name, "..", 2) == 0) continue;

    char fullname[MAXPATHLEN + 1];
    snprintf(fullname, sizeof(fullname), "%s/%s", maildirname, entry->d_name);

    if (is_maildir(fullname)) {
      int mailcount = 0;

      int pnamelen = strlen(fullname);
      if (pnamelen + 4 >= sizeof(fullname)) {
        fprintf(stderr, "Path name too long to access subdirs: %s\n", fullname);
        exit(-1);
      }
      strncpy(fullname + pnamelen, "/new", 4);
      fullname[pnamelen + 4] = '\0';
      mailcount = get_file_count(fullname, 0);

      if (showall || (!showall && !showonlynew)) {
        strncpy(fullname + pnamelen, "/cur", 4);
        fullname[pnamelen + 4] = '\0';
        mailcount += get_file_count(fullname, 1 - showall);
      }

      if (!mailcount) continue;

      /* put the results into a dirnode structure... */
      dp = (struct dirnode*) malloc(sizeof(struct dirnode));
      if (!dp) error_quit(NULL);
      dp->name = (char*) malloc((NAMLEN(entry) + 1) * sizeof(char));
      if (!dp->name) error_quit(NULL);
      strncpy(dp->name, entry->d_name, NAMLEN(entry));
      dp->name[NAMLEN(entry)] = '\0';
      dp->unreadmails = mailcount;

      /* ...and add it to our list. */
      dirlist = (struct dirnode**) realloc(dirlist,
                                           (dirlist_count + 1) *
                                           sizeof(struct dirnode*));
      if (!dirlist) error_quit(NULL);
      dirlist[dirlist_count] = dp;
      dirlist_count++;
    }
  }
  closedir(maildir);

  if (showtotal) {
    j = 0;
    for (i = 0; i < dirlist_count; i++) {
      j += dirlist[i]->unreadmails;
      free(dirlist[i]->name);
      free(dirlist[i]);
    }
    free(dirlist);
    printf("%i\n", j);
    exit(EXIT_SUCCESS);
  }

  qsort(dirlist, dirlist_count, sizeof(struct dirnode*), compare_dirnodes);

  for (i = 0; i < dirlist_count; i++) {
    dp = dirlist[i];

    /* convert dots to slashes */
    if (!muttmode) {
      for(j = 0; j <  strlen(dp->name); j++)
        if (dp->name[j] == '.')
          dp->name[j] = '/';
    }

    if (muttmode) {
      printf("\"=%s\" ", dp->name);
    } else {
      printf("%6i %s\n", dp->unreadmails,
             (strlen(dp->name) == 1 && dp->name[0] == '/')
             ? "[ INBOX ]" : dp->name);
    }
    free(dp->name);
    free(dp);
  }

  free(dirlist);

  exit(EXIT_SUCCESS);
}
