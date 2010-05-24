
/***************************************************************************
 * ncrack_ssh.cc -- ncrack module for the SSH protocol                     *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2010 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                *
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included COPYING.OpenSSL file, and distribute linked      *
 * combinations including the two. You must obey the GNU GPL in all        *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
 * Insecure.Org development mailing lists, it is assumed that you are      *
 * offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
 * non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
 * will always be available Open Source, but this is important because the *
 * inability to relicense code has caused devastating problems for other   *
 * Free Software projects (such as KDE and NASM).  We also occasionally    *
 * relicense the code to third parties as discussed above.  If you wish to *
 * specify special license conditions of your contributions, just say so   *
 * when you send them.                                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License v2.0 for more details at                         *
 * http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
 * included with Nmap.                                                     *
 *                                                                         *
 ***************************************************************************/


#include "ncrack.h"
#include "nsock.h"
#include "NcrackOps.h"
#include "Service.h"
#include "modules.h"
#include <list>

/* OpenSSH include-files */
#include "opensshlib.h"

#include "ssh2.h"
#include "openssl/dh.h"
#include "buffer.h"
#include "kex.h"
#include "sshconnect.h"
#include "packet.h"
#include "misc.h"
#include "cipher.h"
#include "compat.h"


#define SSH_TIMEOUT 20000
#define CLIENT_VERSION "SSH-2.0-OpenSSH_5.2\n"


extern NcrackOps o;

extern void ncrack_read_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_write_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_connect_handler(nsock_pool nsp, nsock_event nse, void *mydata);
extern void ncrack_module_end(nsock_pool nsp, void *mydata);

enum states { SSH_INIT, SSH_ID_EX, SSH_KEY, SSH_KEY2, SSH_KEY3, SSH_KEY4, 
  SSH_AUTH, SSH_AUTH2, SSH_AUTH3, SSH_AUTH4, SSH_FINI };

static void ssh_free(Connection *con);


static inline int
ssh_loop_read(nsock_pool nsp, Connection *con, ncrack_ssh_state *info)
{
  u_int packetlen = 0;
  
  /* If we have data in the I/O buffer, which means that we had previously
   * scheduled an nsock read event, then append the new data we got inside
   * the 'input' buffer which will be processed by the openssh library
   */
  if (con->inbuf != NULL) {
    packetlen = con->inbuf->get_len();
    if (packetlen > 0)
      buffer_append(&info->input, con->inbuf->get_dataptr(), packetlen); 
  }

  info->type = openssh_packet_read(info);
  if (info->type == SSH_MSG_NONE) {
    nsock_read(nsp, con->niod, ncrack_read_handler, SSH_TIMEOUT, con);
    return -1;
  }
  delete con->inbuf;
  con->inbuf = NULL;
  return 0;
}



void
ncrack_ssh(nsock_pool nsp, Connection *con)
{
  nsock_iod nsi = con->niod;
  Service *serv = con->service;
  void *ioptr;
  u_int buflen;
  ncrack_ssh_state *info = NULL;
  con->ops_free = &ssh_free;

  if (con->misc_info)
    info = (ncrack_ssh_state *) con->misc_info;

  switch (con->state)
  {
    case SSH_INIT:

      con->state = SSH_ID_EX;
      con->misc_info = (ncrack_ssh_state *)safe_zalloc(sizeof(ncrack_ssh_state));  
      nsock_read(nsp, nsi, ncrack_read_handler, SSH_TIMEOUT, con);
      break;

    case SSH_ID_EX:

      buflen = con->inbuf->get_len();
      ioptr = con->inbuf->get_dataptr();
      if (!memsearch((const char *)ioptr, "\n", buflen)) {
        con->state = SSH_ID_EX;
        nsock_read(nsp, nsi, ncrack_read_handler, SSH_TIMEOUT, con);
        break;
      }
      if (strncmp((const char *)ioptr, "SSH-", 4)) {
        con->inbuf->clear();
        con->state = SSH_ID_EX;
        nsock_read(nsp, nsi, ncrack_read_handler, SSH_TIMEOUT, con);
        break;
      }
      con->state = SSH_KEY;

      info->server_version_string = Strndup((char *)ioptr, buflen);
      openssh_compat_datafellows(info);

      chop(info->server_version_string);

      /* NEVER forget to free allocated memory and also NULL-assign ptr */
      delete con->inbuf;
      con->inbuf = NULL;

      if (con->outbuf)
        delete con->outbuf;

      con->outbuf = new Buf();
      con->outbuf->append(CLIENT_VERSION, sizeof(CLIENT_VERSION)-1);
      info->client_version_string = Strndup(
          (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());
      chop(info->client_version_string);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con,
          (const char *)con->outbuf->get_dataptr(), con->outbuf->get_len());

      break;

    case SSH_KEY:

      con->state = SSH_KEY2;

      /* sends "Key Exchange Init" */

      /* Initialize cipher contexts and keys as well as internal opensshlib
       * buffers (input, output, incoming_packet, outgoing_packet) 
       */
      packet_set_connection(info);

      openssh_ssh_kex2(info, info->client_version_string,
          info->server_version_string);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output),
          buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;

    case SSH_KEY2:

      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      /* Receives: "Key Exchange Init"
       * Sends: "Diffie-Hellman GEX Request"
       */

      con->state = SSH_KEY3;

      openssh_kex_input_kexinit(info);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output), buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;

    case SSH_KEY3:

      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      /* Receives: "Diffie-Hellman Key Exchange Reply" and
       * Sends: "Diffie-Hellman GEX Init"
       */

      con->state = SSH_KEY4;

      openssh_kexgex_2(info);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output), buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;

    case SSH_KEY4:

      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      /* Receives: "Diffie-Hellman GEX Reply" and
       * Sends: "New keys"
       */

      con->state = SSH_AUTH;

      openssh_kexgex_3(info);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output), buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;

    case SSH_AUTH:

      //printf("SSH AUTH 1\n");

      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      /* Receives: "New keys"
       * Sends "Encrypted Request 1"
       */

      con->state = SSH_AUTH2;

      openssh_start_userauth2(info);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output), buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;

    case SSH_AUTH2:

      //printf("SSH AUTH 2\n");
      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      con->state = SSH_AUTH3;
      /* 
       * If server doesn't support "password" authentication method then
       * there is no point in doing any more attempts, so we can mark the
       * service as finished.
       */
      if (openssh_userauth2_service_rep(info) < 0) {
        serv->end.orly = true;
        if (con->outbuf)
          delete con->outbuf;

        con->outbuf = new Buf();
        Snprintf((char *)con->outbuf->get_dataptr(), DEFAULT_BUF_SIZE,
            "Server denied authentication request: %d", info->type);
        serv->end.reason = Strndup((const char *)con->outbuf->get_dataptr(),
            (size_t)strlen((const char *)con->outbuf->get_dataptr()));
        return ncrack_module_end(nsp, con);
      }

      /* Jump straight to next state */
      nsock_timer_create(nsp, ncrack_timer_handler, 0, con);
      break;

    case SSH_AUTH3:

      /* 
       * Sends credentials
       */
      //printf("SSH AUTH 3\n");
      con->state = SSH_FINI;

      openssh_userauth2(info, con->user, con->pass);

      nsock_write(nsp, nsi, ncrack_write_handler, SSH_TIMEOUT, con, 
          (const char *)buffer_ptr(&info->output), buffer_len(&info->output));
      buffer_consume(&info->output, buffer_len(&info->output));

      break;
      
    case SSH_FINI:

      //printf("SSH FINI\n");
      if (ssh_loop_read(nsp, con, info) < 0)
        break;

      /* 
       * If we get a disconnect message at this stage, then it probably
       * means that we reached the server's authentication limit per
       * connection.
       */
      if (info->type == SSH2_MSG_DISCONNECT) {
        return ncrack_module_end(nsp, con);
      }

      if (info->type == SSH2_MSG_USERAUTH_SUCCESS) {
        con->auth_success = true;
        con->state = SSH_AUTH3;
      } else if (info->type == SSH2_MSG_USERAUTH_FAILURE) {
        //printf("failed!\n");
        con->state = SSH_AUTH3;
      } else if (info->type == SSH2_MSG_USERAUTH_BANNER) {
        printf("Got banner!\n");
      }

      return ncrack_module_end(nsp, con);
  }
}


static void
ssh_free(Connection *con)
{
  ncrack_ssh_state *p;
  if (!con->misc_info)
    return;

  p = (ncrack_ssh_state *)con->misc_info;

  if (p->kex) {
    if (p->kex->peer.alloc > 0)
      free(p->kex->peer.buf);
    if (p->kex->my.alloc > 0)
      free(p->kex->my.buf);
    free(p->kex); 
  }

  /* Note that DH *dh has already been freed from
   * the openssh library */

  /* 2 keys */
  for (int i = 0; i < 2; i++) {
    if (p->keys[i]) {
      free(p->keys[i]->enc.iv);
      free(p->keys[i]->enc.key);
      free(p->keys[i]->mac.key);
      free(p->keys[i]);
    }
  }

  EVP_CIPHER_CTX_cleanup(&p->receive_context.evp);
  EVP_CIPHER_CTX_cleanup(&p->send_context.evp);

  buffer_free(&p->ncrack_buf);
  buffer_free(&p->input);
  buffer_free(&p->output);
  buffer_free(&p->incoming_packet);
  buffer_free(&p->outgoing_packet);

  if (p->client_version_string)
    free(p->client_version_string);
  if (p->server_version_string)
    free(p->server_version_string);
  if (p->disc_reason)
    free(p->disc_reason);

}

