/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@nict.go.jp>							 *
*													 *
* Copyright (c) 2009, WIDE Project and NICT								 *
* All rights reserved.											 *
* 													 *
* Redistribution and use of this software in source and binary forms, with or without modification, are  *
* permitted provided that the following conditions are met:						 *
* 													 *
* * Redistributions of source code must retain the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer.										 *
*    													 *
* * Redistributions in binary form must reproduce the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer in the documentation and/or other						 *
*   materials provided with the distribution.								 *
* 													 *
* * Neither the name of the WIDE Project or NICT nor the 						 *
*   names of its contributors may be used to endorse or 						 *
*   promote products derived from this software without 						 *
*   specific prior written permission of WIDE Project and 						 *
*   NICT.												 *
* 													 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A *
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 	 *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 	 *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR *
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF   *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.								 *
*********************************************************************************************************/

#include "fD.h"

/* Initialize a connection context */
struct cnxctx * fd_cnx_init(int sock, int proto)
{
	struct cnxctx * conn = NULL;
	
	TRACE_ENTRY("%d %d", sock, proto);
	CHECK_PARAMS_DO( (proto == IPPROTO_TCP) || (proto == IPPROTO_SCTP), return NULL);
	
	CHECK_MALLOC_DO( conn = malloc(sizeof(struct cnxctx)), return NULL );
	memset(conn, 0, sizeof(struct cnxctx));
	
	conn->cc_socket = sock;
	conn->cc_proto  = proto;
	
	fd_list_init(&conn->cc_ep_remote, conn);
	fd_list_init(&conn->cc_ep_local, conn);
	
	if (proto == IPPROTO_SCTP) {
#ifndef DISABLE_SCTP
		CHECK_FCT_DO( fd_sctp_get_str_info( sock, &conn->cc_sctp_para.str_in, &conn->cc_sctp_para.str_out ),
				{ free(conn); return NULL; } );
		conn->cc_sctp_para.pairs = (conn->cc_sctp_para.str_out < conn->cc_sctp_para.str_in) ? conn->cc_sctp_para.str_out : conn->cc_sctp_para.str_in;
#else /* DISABLE_SCTP */
		ASSERT(0);
#endif /* DISABLE_SCTP */
	}
	
	return conn;
}

/* TLS handshake the connection */
int fd_cnx_handshake(struct cnxctx * conn, int mode)
{
	TRACE_ENTRY( "%p %d", conn, mode);
	CHECK_PARAMS( conn && ( (mode == GNUTLS_CLIENT) || (mode == GNUTLS_SERVER) ) );
	
	/* Save the mode */
	conn->cc_tls_para.mode = mode;
	
	/* Create the master session context */
	CHECK_GNUTLS_DO( gnutls_init (&conn->cc_tls_para.session, mode), return ENOMEM );
	
	/* Set the algorithm suite */
	CHECK_GNUTLS_DO( gnutls_priority_set( conn->cc_tls_para.session, fd_g_config->cnf_sec_data.prio_cache ), return EINVAL );
	
	/* Set the credentials of this side of the connection */
	CHECK_GNUTLS_DO( gnutls_credentials_set (conn->cc_tls_para.session, GNUTLS_CRD_CERTIFICATE, fd_g_config->cnf_sec_data.credentials), return EINVAL );
	
	/* Request the remote credentials as well */
	if (mode == GNUTLS_SERVER) {
		gnutls_certificate_server_set_request (conn->cc_tls_para.session, GNUTLS_CERT_REQUIRE);
	}

	/* Set the socket info in the session */
	gnutls_transport_set_ptr (conn->cc_tls_para.session, (gnutls_transport_ptr_t) conn->cc_socket);

	/* Special case: multi-stream TLS is not natively managed in GNU TLS, we use a wrapper library */
	if ((conn->cc_proto == IPPROTO_SCTP) && (conn->cc_sctp_para.pairs > 0)) {
#ifndef DISABLE_SCTP
		TODO("Initialize the SCTP TLS wrapper");
		TODO("Set the lowat, push and pull functions");
#else /* DISABLE_SCTP */
		ASSERT(0);
#endif /* DISABLE_SCTP */
	}
	
	/* Handshake master session */
	{
		int ret;
		CHECK_GNUTLS_DO( ret = gnutls_handshake(conn->cc_tls_para.session),
			{
				if (TRACE_BOOL(INFO)) {
					fd_log_debug("TLS Handshake failed on socket %d : %s\n", conn->cc_socket, gnutls_strerror(ret));
				}
				return EINVAL;
			} );
		
		/* Now verify the remote credentials are valid -- only simple test here */
		CHECK_GNUTLS_DO( gnutls_certificate_verify_peers2 (conn->cc_tls_para.session, &ret), return EINVAL );
		if (ret) {
			if (TRACE_BOOL(INFO)) {
				fd_log_debug("TLS: Remote certificate invalid on socket %d :\n", conn->cc_socket);
				if (ret & GNUTLS_CERT_INVALID)
					fd_log_debug(" - The certificate is not trusted (unknown CA?)\n");
				if (ret & GNUTLS_CERT_REVOKED)
					fd_log_debug(" - The certificate has been revoked.\n");
				if (ret & GNUTLS_CERT_SIGNER_NOT_FOUND)
					fd_log_debug(" - The certificate hasn't got a known issuer.\n");
				if (ret & GNUTLS_CERT_SIGNER_NOT_CA)
					fd_log_debug(" - The certificate signer is not a CA, or uses version 1, or 3 without basic constraints.\n");
				if (ret & GNUTLS_CERT_INSECURE_ALGORITHM)
					fd_log_debug(" - The certificate signature uses a weak algorithm.\n");
			}
			return EINVAL;
		}
	}
	
	/* Other sessions in case of multi-stream SCTP are resumed from the master */
	if ((conn->cc_proto == IPPROTO_SCTP) && (conn->cc_sctp_para.pairs > 0)) {
#ifndef DISABLE_SCTP
		TODO("Init and resume all additional sessions from the master one.");
#endif /* DISABLE_SCTP */
	}
	
	return 0;
}
