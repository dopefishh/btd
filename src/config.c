#define _GNU_SOURCE

#include <argp.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "misc.h"

const char *argp_program_version = "btd 0.1";
const char *argp_program_bug_address = "<mart@martlubbers.net>";

static char doc[] = "btd -- A BibTex daemon";
static char args_doc[] = "[~/.config/btd/config]";

static struct argp_option options[] = {
	{"verbose",'v', 0, 0, "Increase verbosity", 0},
	{"quiet",'q', 0, 0, "Decrease verbosity", 0},
	{0}
};

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	struct btd_config *config = state->input;

	switch (key)
	{
		case 'q':
			config->verbose -= config->verbose != 0;
			break;
		case 'v':
			config->verbose += config->verbose != 5;
			break;
		case ARGP_KEY_ARG:
			config->configpath = arg;
			break;
		case ARGP_KEY_END:
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {
	.options=options, 
	.parser=parse_opt,
	.args_doc=args_doc,
	.doc=doc,
	.children=NULL,
	.help_filter=NULL,
	.argp_domain=NULL};

static char *resolve_tilde(const char *path) {
	static glob_t globbuf;
	char *head, *tail, *result = NULL;

	tail = strchr(path, '/');
	head = strndup(path, tail ? (size_t)(tail - path) : strlen(path));

	int res = glob(head, GLOB_TILDE, NULL, &globbuf);
	free(head);
	if (res == GLOB_NOMATCH || globbuf.gl_pathc != 1)
		result = safe_strdup(path);
	else if (res != 0) {
		die("glob() failed");
	} else {
		head = globbuf.gl_pathv[0];
		result = calloc(strlen(head) + (tail ? strlen(tail) : 0) + 1, 1);
		strncpy(result, head, strlen(head));
		if(tail){
			strncat(result, tail, strlen(tail));
		}
	}
	globfree(&globbuf);

	return result;
}

static char *get_config_path(char *cp) {
	if(cp != NULL){
		if(path_exists(cp)){
			return cp;
		}
		die("Configuration file doesn't exist");
	}
	char *xdg_config_home, *xdg_config_dirs, *config_path, *buf, *tok;

	// 2: check for $XDG_CONFIG_HOME/btd/config
	if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL){
		xdg_config_home = "~/.config";
	}

	xdg_config_home = resolve_tilde(xdg_config_home);
	if (asprintf(&config_path, "%s/btd/config", xdg_config_home) == -1){
		die("asprintf() failed");
	}
	free(xdg_config_home);

	if (path_exists(config_path)){
		return config_path;
	}
	free(config_path);

	/* 3: Check /etc/btd.conf */
	config_path = "/etc/btd.conf";
	if (path_exists(config_path)){
		return safe_strdup(config_path);
	}

	/* 4: Check $XDG_CONFIG_DIRS/btd/config */
	if ((xdg_config_dirs = getenv("XDG_CONFIG_DIRS")) == NULL){
		xdg_config_dirs = "/etc/xdg";
	}

	buf = safe_strdup(xdg_config_dirs);
	tok = strtok(buf, ":");
	while (tok != NULL) {
		tok = resolve_tilde(tok);
		if (asprintf(&config_path, "%s/btd/config", tok) == -1){
			die("asprintf() failed");
		}
		free(tok);
		if (path_exists(config_path)) {
			free(buf);
			return config_path;
		}
		free(config_path);
		tok = strtok(NULL, ":");
	}
	free(buf);

	die("Unable to find the configuration file");
	return NULL;
}

void update_config(struct btd_config *config, char *key, char *value){
	key = rtrim(ltrim(key));
	value = rtrim(ltrim(value));

	/* If the key is empty or the line starts with a comment we skip */
	if(key[0] == '#' || strlen(key) == 0){
		return;
	}

	/* If the key contains a comment we skip */
	if(strchr("key", (char)'#') != NULL){
		return;
	}
	value[strcspn(value, "#")] = '\0';

	/* If the value stripped of comments is empty we skip */
	if(strlen(value) == 0){
		return;
	}

	/* Configuration options */
	if(strcmp(key, "socket") == 0){
		config->socket = resolve_tilde(value);
	} else if(strcmp(key, "db") == 0){
		config->db = resolve_tilde(value);
	}
}

void btd_config_populate(struct btd_config *config, int argc, char **argv)
{
	FILE *fp;
	char *key, *line = NULL;
	size_t len, sep;

	config->configpath = NULL;
	config->verbose = 1;

	config->socket = "/var/run/btd.socket";
	argp_parse(&argp, argc, argv, 0, 0, config);
	config->configpath = get_config_path(config->configpath);

	fp = fopen(config->configpath, "r");

	while(getline(&line, &len, fp) != -1) {
		sep = strcspn(line, "=");
		key = strndup(line, sep);
		if(key == NULL){
			die("strndup() failed");
		}
		update_config(config, key, line+sep+1);
		free(key);
		free(line);
		line = NULL;
	}

	fclose(fp);
}

void btd_config_print(struct btd_config *config, FILE *fp){
	fprintf(fp, 
		"BTD Config digest\n"
		"-----------------\n"
		"configpath: '%s'\n"
		"verbosity: '%d'\n"
		"\n"
		"socket: '%s'\n"
		"db: '%s'\n", 
			config->configpath,
			config->verbose,
			config->socket,
			config->db);
}