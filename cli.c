#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <getopt.h>

#include <availability.h>
#include <uuid/uuid.h>

#include "cli.h"

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 101500
#error "Requires macOS 10.15 or later"
#endif

#define CLI_DEFAULT_VDE_GROUP "staff"

static void print_usage(const char *argv0) {
  printf("Usage: %s [OPTION]... VDESWITCH\n", argv0);
  printf("vmnet.framework support for rootless QEMU.\n");
  printf("vde_vmnet does not require QEMU to run as the root user, but "
         "vde_vmnet itself has to run as the root, in most cases.\n");
  printf("\n");
  printf("--vde-group=GROUP                   VDE group name (default: "
         "\"" CLI_DEFAULT_VDE_GROUP "\")\n");
  printf(
      "--vmnet-mode=(host|shared|bridged)  vmnet mode (default: \"shared\")\n");
  printf("--vmnet-interface=INTERFACE         interface used for "
         "--vmnet=bridged, e.g., \"en0\"\n");
  printf("--vmnet-gateway=IP                  gateway used for "
         "--vmnet=(host|shared), e.g., \"192.168.105.1\" (default: decided by "
         "macOS)\n");
  printf("                                    the next IP (e.g., "
         "\"192.168.105.2\") is used as the first DHCP address\n");
  printf("--vmnet-dhcp-end=IP                 end of the DHCP range (default: "
         "XXX.XXX.XXX.254)\n");
  printf("                                    requires --vmnet-gateway to be "
         "specified\n");
  printf("--vmnet-mask=MASK                   subnet mask (default: "
         "\"255.255.255.0\")\n");
  printf("                                    requires --vmnet-gateway to be "
         "specified\n");
  printf("--vmnet-interface-id=UUID           vmnet interface ID (default: "
         "random)\n");
  printf("-h, --help                          display this help and exit\n");
  printf("-v, --version                       display version information and "
         "exit\n");
  printf("\n");
  printf("version: " VERSION "\n");
}

static void print_version() { puts(VERSION); }

#define CLI_OPTIONS_ID_VDE_GROUP -42
#define CLI_OPTIONS_ID_VMNET_MODE -43
#define CLI_OPTIONS_ID_VMNET_INTERFACE -44
#define CLI_OPTIONS_ID_VMNET_GATEWAY -45
#define CLI_OPTIONS_ID_VMNET_DHCP_END -46
#define CLI_OPTIONS_ID_VMNET_MASK -47
#define CLI_OPTIONS_ID_VMNET_INTERFACE_ID -48
struct cli_options *cli_options_parse(int argc, char *argv[]) {
  struct cli_options *res = malloc(sizeof(*res));
  if (res == NULL) {
    goto error;
  }
  memset(res, 0, sizeof(*res));

  const struct option longopts[] = {
      {"vde-group", required_argument, NULL, CLI_OPTIONS_ID_VDE_GROUP},
      {"vmnet-mode", required_argument, NULL, CLI_OPTIONS_ID_VMNET_MODE},
      {"vmnet-interface", required_argument, NULL,
       CLI_OPTIONS_ID_VMNET_INTERFACE},
      {"vmnet-gateway", required_argument, NULL, CLI_OPTIONS_ID_VMNET_GATEWAY},
      {"vmnet-dhcp-end", required_argument, NULL,
       CLI_OPTIONS_ID_VMNET_DHCP_END},
      {"vmnet-mask", required_argument, NULL, CLI_OPTIONS_ID_VMNET_MASK},
      {"vmnet-interface-id", required_argument, NULL,
       CLI_OPTIONS_ID_VMNET_INTERFACE_ID},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'v'},
      {0, 0, 0, 0},
  };
  int opt = 0;
  while ((opt = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
    switch (opt) {
    case CLI_OPTIONS_ID_VDE_GROUP:
      res->vde_group = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_MODE:
      if (strcmp(optarg, "host") == 0) {
        res->vmnet_mode = VMNET_HOST_MODE;
      } else if (strcmp(optarg, "shared") == 0) {
        res->vmnet_mode = VMNET_SHARED_MODE;
      } else if (strcmp(optarg, "bridged") == 0) {
        res->vmnet_mode = VMNET_BRIDGED_MODE;
      } else {
        fprintf(stderr, "Unknown vmnet mode \"%s\"\n", optarg);
        goto error;
      }
      break;
    case CLI_OPTIONS_ID_VMNET_INTERFACE:
      res->vmnet_interface = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_GATEWAY:
      res->vmnet_gateway = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_DHCP_END:
      res->vmnet_dhcp_end = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_MASK:
      res->vmnet_mask = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_INTERFACE_ID:
      if (uuid_parse(optarg, res->vmnet_interface_id) < 0) {
        fprintf(stderr, "Failed to parse UUID \"%s\"\n", optarg);
        goto error;
      }
      break;
    case 'h':
      print_usage(argv[0]);
      cli_options_destroy(res);
      exit(EXIT_SUCCESS);
      return NULL;
      break;
    case 'v':
      print_version();
      cli_options_destroy(res);
      exit(EXIT_SUCCESS);
      return NULL;
      break;
    default:
      goto error;
      break;
    }
  }
  if (argc - optind != 1) {
    goto error;
  }
  res->vde_switch = strdup(argv[optind]);

  /* fill default */
  if (res->vde_group == NULL)
    res->vde_group =
        strdup(CLI_DEFAULT_VDE_GROUP); /* use strdup to make it freeable */
  if (res->vmnet_mode == 0)
    res->vmnet_mode = VMNET_SHARED_MODE;
  if (res->vmnet_gateway != NULL && res->vmnet_dhcp_end == NULL) {
    /* Set default vmnet_dhcp_end to XXX.XXX.XXX.254 (only when --vmnet-gateway
     * is specified) */
    struct in_addr sin;
    if (!inet_aton(res->vmnet_gateway, &sin)) {
      perror("inet_aton(res->vmnet_gateway)");
      goto error;
    }
    uint32_t h = ntohl(sin.s_addr);
    h &= 0xFFFFFF00;
    h |= 0x000000FE;
    sin.s_addr = htonl(h);
    const char *end_static = inet_ntoa(sin); /* static storage, do not free */
    if (end_static == NULL) {
      perror("inet_ntoa");
      goto error;
    }
    res->vmnet_dhcp_end = strdup(end_static);
  }
  if (res->vmnet_gateway != NULL && res->vmnet_mask == NULL)
    res->vmnet_mask =
        strdup("255.255.255.0"); /* use strdup to make it freeable */
  if (uuid_is_null(res->vmnet_interface_id)) {
    uuid_generate_random(res->vmnet_interface_id);
  }

  /* validate */
  if (res->vmnet_mode == VMNET_BRIDGED_MODE && res->vmnet_interface == NULL) {
    fprintf(
        stderr,
        "vmnet mode \"bridged\" require --vmnet-interface to be specified\n");
    goto error;
  }
  if (res->vmnet_gateway == NULL) {
    if (res->vmnet_mode != VMNET_BRIDGED_MODE) {
      fprintf(stderr,
              "WARNING: --vmnet-gateway=IP should be explicitly specified to "
              "avoid conflicting with other applications\n");
    }
    if (res->vmnet_dhcp_end != NULL) {
      fprintf(stderr, "--vmnet-dhcp-end=IP requires --vmnet-gateway=IP\n");
      goto error;
    }
    if (res->vmnet_mask != NULL) {
      fprintf(stderr, "--vmnet-mask=MASK requires --vmnet-gateway=IP\n");
      goto error;
    }
  } else {
    if (res->vmnet_mode == VMNET_BRIDGED_MODE) {
      fprintf(stderr,
              "vmnet mode \"bridged\" conflicts with --vmnet-gateway\n");
      goto error;
    }
    struct in_addr dummy;
    if (!inet_aton(res->vmnet_gateway, &dummy)) {
      fprintf(stderr,
              "invalid address \"%s\" was specified for --vmnet-gateway\n",
              res->vmnet_gateway);
      goto error;
    }
  }
  return res;
error:
  print_usage(argv[0]);
  cli_options_destroy(res);
  exit(EXIT_FAILURE);
  return NULL;
}

void cli_options_destroy(struct cli_options *x) {
  if (x == NULL)
    return;
  if (x->vde_group != NULL)
    free(x->vde_group);
  if (x->vde_switch != NULL)
    free(x->vde_switch);
  if (x->vmnet_interface != NULL)
    free(x->vmnet_interface);
  if (x->vmnet_gateway != NULL)
    free(x->vmnet_gateway);
  if (x->vmnet_dhcp_end != NULL)
    free(x->vmnet_dhcp_end);
  if (x->vmnet_mask != NULL)
    free(x->vmnet_mask);
  free(x);
}
