/* t-keylist.c  - regression test
   Copyright (C) 2000 Werner Koch (dd9jn)
   Copyright (C) 2001, 2003 g10 Code GmbH

   This file is part of GPGME.
 
   GPGME is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
 
   GPGME is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with GPGME; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gpgme.h>


#define fail_if_err(err)					\
  do								\
    {								\
      if (err)							\
        {							\
          fprintf (stderr, "%s:%d: gpgme_error_t %s\n",		\
                   __FILE__, __LINE__, gpgme_strerror (err));   \
          exit (1);						\
        }							\
    }								\
  while (0)


struct
{
  char *fpr;
  int secret;
  long timestamp;
  long expires;
  char *issuer_serial;
  char *issuer_name;
  char *chain_id;
  char *uid;
}
keys[] =
  {
    { "3CF405464F66ED4A7DF45BBDD1E4282E33BDB76E", 1, 1007372198, 1038908198, "00",
      "CN=test cert 1,OU=Aegypten Project,O=g10 Code GmbH,L=D\xc3\xbcsseldorf,C=DE",
      NULL,
      "CN=test cert 1,OU=Aegypten Project,O=g10 Code GmbH,L=D\xc3\xbcsseldorf,C=DE" },
    { "DFA56FB5FC41E3A8921F77AD1622EEFD9152A5AD", 0, 909684190, 1009821790, "01",
      "1.2.840.113549.1.9.1=#63657274696679407063612E64666E2E6465,"
      "CN=DFN Top Level Certification Authority,OU=DFN-PCA,"
      "O=Deutsches Forschungsnetz,C=DE", NULL,
      "1.2.840.113549.1.9.1=#63657274696679407063612E64666E2E6465,"
      "CN=DFN Top Level Certification Authority,OU=DFN-PCA,"
      "O=Deutsches Forschungsnetz,C=DE" },
    { "2C8F3C356AB761CB3674835B792CDA52937F9285", 0, 973183644, 1009735200, "15",
      "1.2.840.113549.1.9.1=#63657274696679407063612E64666E2E6465,"
      "CN=DFN Top Level Certification Authority,OU=DFN-PCA,"
      "O=Deutsches Forschungsnetz,C=DE",
      "DFA56FB5FC41E3A8921F77AD1622EEFD9152A5AD",
      "1.2.840.113549.1.9.1=#63657274696679407063612E64666E2E6465,"
      "CN=DFN Server Certification Authority,OU=DFN-PCA,"
      "O=Deutsches Forschungsnetz,C=DE" },
    { NULL }
  };

int 
main (int argc, char **argv)
{
  gpgme_error_t err;
  gpgme_ctx_t ctx;
  gpgme_key_t key;
  gpgme_keylist_result_t result;
  int i = 0;

  err = gpgme_engine_check_version (GPGME_PROTOCOL_CMS);
  fail_if_err (err);

  err = gpgme_new (&ctx);
  fail_if_err (err);
  gpgme_set_protocol (ctx, GPGME_PROTOCOL_CMS);

  err = gpgme_op_keylist_start (ctx, NULL, 0);
  fail_if_err (err);
    
  while (!(err = gpgme_op_keylist_next (ctx, &key)))
    {
      if (!keys[i].fpr)
	{
	  fprintf (stderr, "More keys returned than expected\n");
	  exit (1);
	}

      /* Global key flags.  */
      if (key->revoked)
	{
	  fprintf (stderr, "Key unexpectedly revoked\n");
	  exit (1);
	}
      if (key->expired)
	{
	  fprintf (stderr, "Key unexpectedly expired\n");
	  exit (1);
	}
      if (key->disabled)
	{
	  fprintf (stderr, "Key unexpectedly disabled\n");
	  exit (1);
	}
      if (key->invalid)
	{
	  fprintf (stderr, "Key unexpectedly invalid\n");
	  exit (1);
	}
      if (key->can_encrypt != keys[i].secret)
	{
	  fprintf (stderr, "Key unexpectedly%s usable for encryption\n",
		   key->can_encrypt ? "" : " not");
	  exit (1);
	}
      if (key->can_sign != keys[i].secret)
	{
	  fprintf (stderr, "Key unexpectedly%s usable for signing\n",
		   key->can_sign ? "" : " not");
	  exit (1);
	}
      if (!key->can_certify)
	{
	  fprintf (stderr, "Key unexpectedly unusable for certifications\n");
	  exit (1);
	}
      if (key->secret != keys[i].secret)
	{
	  fprintf (stderr, "Key unexpectedly%s secret\n",
		   key->secret ? "" : " not");
	  exit (1);
	}
      if (key->protocol != GPGME_PROTOCOL_CMS)
	{
	  fprintf (stderr, "Key has unexpected protocol: %s\n",
		   gpgme_get_protocol_name (key->protocol));
	  exit (1);
	}
      if (!key->issuer_serial)
	{
	  fprintf (stderr, "Key unexpectedly misses issuer serial\n");
	  exit (1);
	}
      if (strcmp (key->issuer_serial, keys[i].issuer_serial))
	{
	  fprintf (stderr, "Key has unexpected issuer serial: %s\n",
		   key->issuer_serial);
	  exit (1);
	}
      if (!key->issuer_name)
	{
	  fprintf (stderr, "Key unexpectedly misses issuer name\n");
	  exit (1);
	}
      if (strcmp (key->issuer_name, keys[i].issuer_name))
	{
	  fprintf (stderr, "Key has unexpected issuer name: %s\n",
		   key->issuer_name);
	  exit (1);
	}
      if (key->chain_id && !keys[i].chain_id)
	{
	  fprintf (stderr, "Key unexpectedly carries chain ID: %s\n",
		   key->chain_id);
	  exit (1);
	}
      if (!key->chain_id && keys[i].chain_id)
	{
	  fprintf (stderr, "Key unexpectedly carries no chain ID\n");
	  exit (1);
	}
      if (key->chain_id && strcmp (key->chain_id, keys[i].chain_id))
	{
	  fprintf (stderr, "Key carries unexpected chain ID: %s\n",
		   key->chain_id);
	  exit (1);
	}
      if (key->owner_trust != GPGME_VALIDITY_UNKNOWN)
	{
	  fprintf (stderr, "Key has unexpected owner trust: %i\n",
		   key->owner_trust);
	  exit (1);
	}
      if (!key->subkeys || key->subkeys->next)
	{
	  fprintf (stderr, "Key has unexpected number of subkeys\n");
	  exit (1);
	}

      /* Primary key.  */
      if (key->subkeys->revoked)
	{
	  fprintf (stderr, "Primary key unexpectedly revoked\n");
	  exit (1);
	}
      if (key->subkeys->expired)
	{
	  fprintf (stderr, "Primary key unexpectedly expired\n");
	  exit (1);
	}
      if (key->subkeys->disabled)
	{
	  fprintf (stderr, "Primary key unexpectedly disabled\n");
	  exit (1);
	}
      if (key->subkeys->invalid)
	{
	  fprintf (stderr, "Primary key unexpectedly invalid\n");
	  exit (1);
	}
      if (key->subkeys->can_encrypt != keys[i].secret)
	{
	  fprintf (stderr, "Key unexpectedly%s usable for encryption\n",
		   key->subkeys->can_encrypt ? "" : " not");
	  exit (1);
	}
      if (key->subkeys->can_sign != keys[i].secret)
	{
	  fprintf (stderr, "Key unexpectedly%s usable for signing\n",
		   key->subkeys->can_sign ? "" : " not");
	  exit (1);
	}
      if (!key->subkeys->can_certify)
	{
	  fprintf (stderr, "Primary key unexpectedly unusable for certifications\n");
	  exit (1);
	}
      if (key->subkeys->secret)
	{
	  fprintf (stderr, "Primary key unexpectedly secret\n");
	  exit (1);
	}
      if (key->subkeys->pubkey_algo != GPGME_PK_RSA)
	{
	  fprintf (stderr, "Primary key has unexpected public key algo: %s\n",
		   gpgme_pubkey_algo_name (key->subkeys->pubkey_algo));
	  exit (1);
	}
      if (key->subkeys->length != 1024)
	{
	  fprintf (stderr, "Primary key has unexpected length: %i\n",
		   key->subkeys->length);
	  exit (1);
	}
      if (strcmp (key->subkeys->keyid, &keys[i].fpr[40 - 16]))
	{
	  fprintf (stderr, "Primary key has unexpected key ID: %s\n",
		   key->subkeys->keyid);
	  exit (1);
	}
      if (strcmp (key->subkeys->fpr, keys[i].fpr))
	{
	  fprintf (stderr, "Primary key has unexpected fingerprint: %s\n",
		   key->subkeys->fpr);
	  exit (1);
	}
      if (key->subkeys->timestamp != keys[i].timestamp)
	{
	  fprintf (stderr, "Primary key unexpected timestamp: %lu\n",
		   key->subkeys->timestamp);
	  exit (1);
	}
      if (key->subkeys->expires != keys[i].expires)
	{
	  fprintf (stderr, "Primary key unexpectedly expires: %lu\n",
		   key->subkeys->expires);
	  exit (1);
	}

      if (!key->uids || key->uids->next)
	{
	  fprintf (stderr, "Key has unexpected number of user IDs\n");
	  exit (1);
	}
      if (key->uids->revoked)
	{
	  fprintf (stderr, "User ID unexpectedly revoked\n");
	  exit (1);
	}
      if (key->uids->invalid)
	{
	  fprintf (stderr, "User ID unexpectedly invalid\n");
	  exit (1);
	}
      if (key->uids->validity != GPGME_VALIDITY_UNKNOWN)
	{
	  fprintf (stderr, "User ID unexpectedly validity: %i\n",
		   key->uids->validity);
	  exit (1);
	}
      if (key->uids->signatures)
	{
	  fprintf (stderr, "User ID unexpectedly signed\n");
	  exit (1);
	}
      if (!key->uids->name || key->uids->name[0])
	{
	  fprintf (stderr, "Unexpected name in user ID: %s\n",
		   key->uids->name);
	  exit (1);
	}
      if (!key->uids->comment || key->uids->comment[0])
	{
	  fprintf (stderr, "Unexpected comment in user ID: %s\n",
		   key->uids->comment);
	  exit (1);
	}
      if (!key->uids->email || key->uids->email[0])
	{
	  fprintf (stderr, "Unexpected email in user ID: %s\n",
		   key->uids->email);
	  exit (1);
	}
      if (!key->uids->uid || strcmp (key->uids->uid, keys[i].uid))
	{
	  fprintf (stderr, "Unexpected uid in user ID: %s\n",
		   key->uids->uid);
	  exit (1);
	}

      gpgme_key_unref (key);
      i++;
    }
  if (gpg_err_code (err) != GPG_ERR_EOF)
    fail_if_err (err);
  err = gpgme_op_keylist_end (ctx);
  fail_if_err (err);

  result = gpgme_op_keylist_result (ctx);
  if (result->truncated)
    {
      fprintf (stderr, "Key listing unexpectedly truncated\n");
      exit (1);
    }

  if (keys[i].fpr)
    {
      fprintf (stderr, "Less keys returned than expected\n");
      exit (1);
    }

  gpgme_release (ctx);
  return 0;
}