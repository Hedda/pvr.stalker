/*
 *      Copyright (C) 2015  Jamal Edey
 *      http://www.kenshisoft.com/
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Kodi; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 *  http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 */

#include "request.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "stb.h"
#include "itv.h"
#include "watchdog.h"
#include "util.h"

void sc_request_set_missing_required(sc_param_request_t *dst_params, sc_param_request_t *src_params) {
  sc_param_t *src_param;
  sc_param_t *dst_param;

  src_param = src_params->param;
  while (src_param) {
    if (!(dst_param = sc_param_get(dst_params, src_param->name))
      && src_param->required) {
      sc_param_t *copy = NULL;
      if ((copy = sc_param_copy(src_param))) {
        fprintf(stderr, "appending %s\n", src_param->name);
        sc_param_append(dst_params, copy);
      }
    }

    src_param = src_param->next;
  }
}

void sc_request_remove_default_non_required(sc_param_request_t *dst_params, sc_param_request_t *src_params) {
  sc_param_t *src_param;
  sc_param_t *dst_param;

  src_param = src_params->param;
  while (src_param) {
    bool destroy = true;

    if ((dst_param = sc_param_get(dst_params, src_param->name))) {
      switch (src_param->type) {
        case SC_STRING:
          if (strcmp(dst_param->value.string, src_param->value.string) != 0) {
            free(dst_param->value.string);
            dst_param->value.string = src_param->value.string;
            destroy = false;
          }
          break;
        case SC_INTEGER:
          if (dst_param->value.integer != src_param->value.integer) {
            dst_param->value.integer = src_param->value.integer;
            destroy = false;
          }
          break;
        case SC_BOOLEAN:
          if (dst_param->value.boolean != src_param->value.boolean) {
            dst_param->value.boolean = src_param->value.boolean;
            destroy = false;
          }
          break;
      }

      if (!dst_param->required && destroy) {
        fprintf(stderr, "destroying %s\n", dst_param->name);
        sc_param_destroy(dst_params, dst_param);
      }
    }

    src_param = src_param->next;
  }
}

sc_request_nameVal_t* sc_request_create_nameVal(const char *name, char *value) {
  sc_request_nameVal_t *header;

  header = (sc_request_nameVal_t *) malloc(sizeof (sc_request_nameVal_t));
  header->name = name;
  header->value = sc_util_strcpy(value);

  header->first = NULL;
  header->prev = NULL;
  header->next = NULL;

  return header;
}

sc_request_nameVal_t* sc_request_link_nameVal(sc_request_nameVal_t *a, sc_request_nameVal_t *b) {
  b->first = a->first;
  b->prev = a;
  a->next = b;

  return b;
}

void sc_request_append_nameVal(sc_request_t *request, sc_request_nameVal_t *header) {
  sc_request_nameVal_t *oheader;

  if (!request->headers) {
    request->headers = header;
  } else {
    oheader = request->headers;
    while (oheader && oheader->next) {
      oheader = oheader->next;
    }

    sc_request_link_nameVal(oheader, header);
  }

  header->next = NULL;
}

void sc_request_build_headers(sc_identity_t *identity, sc_request_t *request, sc_action_t action) {
  sc_request_nameVal_t *header;
  char buffer[256];

  memset(&buffer, 0, sizeof (buffer));
  sprintf(buffer, "mac=%s; stb_lang=%s; timezone=%s",
    identity->mac, identity->lang, identity->time_zone);
  header = sc_request_create_nameVal("Cookie", buffer);

  if (!request->headers) {
    header->first = header;
    request->headers = header;
  } else {
    header = sc_request_link_nameVal(request->headers, header);
  }

  if (action != STB_HANDSHAKE) {
    memset(&buffer, 0, sizeof (buffer));
    sprintf(buffer, "Bearer %s", identity->token);
    header = sc_request_link_nameVal(header, sc_request_create_nameVal("Authorization", buffer));
  }

  header->next = NULL;
  header = NULL;
}

void sc_request_build_query_params(sc_param_request_t *params, sc_request_t *request) {
  sc_param_t *param;
  char buffer[1024];
  sc_request_nameVal_t *qParamPrev;
  sc_request_nameVal_t *qParam;

  qParamPrev = request->params;
  while (qParamPrev && qParamPrev->next)
    qParamPrev = qParamPrev->next;

  param = params->param;
  while (param) {
    memset(&buffer, 0, sizeof (buffer));

    switch (param->type) {
      case SC_STRING:
        sprintf(buffer, "%s", param->value.string);
        break;
      case SC_INTEGER:
        sprintf(buffer, "%d", param->value.integer);
        break;
      case SC_BOOLEAN:
        sprintf(buffer, "%d", param->value.boolean ? 1 : 0);
        break;
    }

    qParam = sc_request_create_nameVal(param->name, buffer);

    if (!request->params) {
      qParam->first = qParam;
      request->params = qParamPrev = qParam;
    } else {
      qParamPrev = sc_request_link_nameVal(qParamPrev, qParam);
    }

    param = param->next;
  }

  qParamPrev->next = NULL;
}

bool sc_request_build(sc_identity_t *identity, sc_param_request_t *params, sc_request_t *request) {
  sc_param_request_t *final_params;
  bool result = true;

  final_params = (sc_param_request_t *) malloc(sizeof (sc_param_request_t));
  memset(final_params, 0, sizeof (*final_params));
  final_params->action = params->action;

  switch (final_params->action) {
    case STB_HANDSHAKE:
    case STB_GET_PROFILE:
    case STB_DO_AUTH:
      if (!sc_stb_defaults(final_params)
        || !sc_stb_prep_request(params, request))
        result = false;
      break;
    case ITV_GET_ALL_CHANNELS:
    case ITV_GET_ORDERED_LIST:
    case ITV_CREATE_LINK:
    case ITV_GET_GENRES:
    case ITV_GET_EPG_INFO:
      if (!sc_itv_defaults(final_params)
        || !sc_itv_prep_request(params, request))
        result = false;
      break;
    case WATCHDOG_GET_EVENTS:
      if (!sc_watchdog_defaults(final_params)
        || !sc_watchdog_prep_request(params, request))
        result = false;
      break;
  }

  if (result) {
    sc_request_set_missing_required(params, final_params);
    sc_request_remove_default_non_required(final_params, params);

    sc_request_build_headers(identity, request, final_params->action);
    sc_request_build_query_params(final_params, request);
  }
  
  free(final_params);
  final_params = NULL;

  return result;
}

void sc_request_free_nameVal(sc_request_nameVal_t *header) {
  free(header->value);
  free(header);
  header = NULL;
}

void sc_request_free_nameVals(sc_request_nameVal_t *header) {
  while (header) {
    sc_request_nameVal_t *next;
    next = header->next;

    sc_request_free_nameVal(header);

    header = next;
  }
}

void sc_request_free(sc_request_t *request) {
  sc_request_free_nameVals(request->headers);
  free(request);
  request = NULL;
}
