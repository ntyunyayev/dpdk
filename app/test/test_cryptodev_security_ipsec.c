/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(C) 2021 Marvell.
 */

#include <rte_common.h>
#include <rte_cryptodev.h>
#include <rte_ip.h>
#include <rte_security.h>

#include "test.h"
#include "test_cryptodev_security_ipsec.h"

int
test_ipsec_sec_caps_verify(struct rte_security_ipsec_xform *ipsec_xform,
			   const struct rte_security_capability *sec_cap,
			   bool silent)
{
	/* Verify security capabilities */

	if (ipsec_xform->options.esn == 1 && sec_cap->ipsec.options.esn == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "ESN is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.udp_encap == 1 &&
	    sec_cap->ipsec.options.udp_encap == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "UDP encapsulation is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.copy_dscp == 1 &&
	    sec_cap->ipsec.options.copy_dscp == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "Copy DSCP is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.copy_flabel == 1 &&
	    sec_cap->ipsec.options.copy_flabel == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "Copy Flow Label is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.copy_df == 1 &&
	    sec_cap->ipsec.options.copy_df == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "Copy DP bit is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.dec_ttl == 1 &&
	    sec_cap->ipsec.options.dec_ttl == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "Decrement TTL is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.ecn == 1 && sec_cap->ipsec.options.ecn == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "ECN is not supported\n");
		return -ENOTSUP;
	}

	if (ipsec_xform->options.stats == 1 &&
	    sec_cap->ipsec.options.stats == 0) {
		if (!silent)
			RTE_LOG(INFO, USER1, "Stats is not supported\n");
		return -ENOTSUP;
	}

	return 0;
}

int
test_ipsec_crypto_caps_aead_verify(
		const struct rte_security_capability *sec_cap,
		struct rte_crypto_sym_xform *aead)
{
	const struct rte_cryptodev_symmetric_capability *sym_cap;
	const struct rte_cryptodev_capabilities *crypto_cap;
	int j = 0;

	while ((crypto_cap = &sec_cap->crypto_capabilities[j++])->op !=
			RTE_CRYPTO_OP_TYPE_UNDEFINED) {
		if (crypto_cap->op == RTE_CRYPTO_OP_TYPE_SYMMETRIC &&
				crypto_cap->sym.xform_type == aead->type &&
				crypto_cap->sym.aead.algo == aead->aead.algo) {
			sym_cap = &crypto_cap->sym;
			if (rte_cryptodev_sym_capability_check_aead(sym_cap,
					aead->aead.key.length,
					aead->aead.digest_length,
					aead->aead.aad_length,
					aead->aead.iv.length) == 0)
				return 0;
		}
	}

	return -ENOTSUP;
}

void
test_ipsec_td_in_from_out(const struct ipsec_test_data *td_out,
			  struct ipsec_test_data *td_in)
{
	memcpy(td_in, td_out, sizeof(*td_in));

	/* Populate output text of td_in with input text of td_out */
	memcpy(td_in->output_text.data, td_out->input_text.data,
	       td_out->input_text.len);
	td_in->output_text.len = td_out->input_text.len;

	/* Populate input text of td_in with output text of td_out */
	memcpy(td_in->input_text.data, td_out->output_text.data,
	       td_out->output_text.len);
	td_in->input_text.len = td_out->output_text.len;

	td_in->ipsec_xform.direction = RTE_SECURITY_IPSEC_SA_DIR_INGRESS;

	if (td_in->aead) {
		td_in->xform.aead.aead.op = RTE_CRYPTO_AEAD_OP_DECRYPT;
	} else {
		td_in->xform.chain.auth.auth.op = RTE_CRYPTO_AUTH_OP_VERIFY;
		td_in->xform.chain.cipher.cipher.op =
				RTE_CRYPTO_CIPHER_OP_DECRYPT;
	}
}

static int
test_ipsec_tunnel_hdr_len_get(const struct ipsec_test_data *td)
{
	int len = 0;

	if (td->ipsec_xform.direction == RTE_SECURITY_IPSEC_SA_DIR_EGRESS) {
		if (td->ipsec_xform.mode == RTE_SECURITY_IPSEC_SA_MODE_TUNNEL) {
			if (td->ipsec_xform.tunnel.type ==
					RTE_SECURITY_IPSEC_TUNNEL_IPV4)
				len += sizeof(struct rte_ipv4_hdr);
			else
				len += sizeof(struct rte_ipv6_hdr);
		}
	}

	return len;
}

static int
test_ipsec_td_verify(struct rte_mbuf *m, const struct ipsec_test_data *td,
		     bool silent)
{
	uint8_t *output_text = rte_pktmbuf_mtod(m, uint8_t *);
	uint32_t skip, len = rte_pktmbuf_pkt_len(m);

	if (len != td->output_text.len) {
		printf("Output length (%d) not matching with expected (%d)\n",
			len, td->output_text.len);
		return TEST_FAILED;
	}

	skip = test_ipsec_tunnel_hdr_len_get(td);

	len -= skip;
	output_text += skip;

	if (memcmp(output_text, td->output_text.data + skip, len)) {
		if (silent)
			return TEST_FAILED;

		printf("TestCase %s line %d: %s\n", __func__, __LINE__,
			"output text not as expected\n");

		rte_hexdump(stdout, "expected", td->output_text.data + skip,
			    len);
		rte_hexdump(stdout, "actual", output_text, len);
		return TEST_FAILED;
	}

	return TEST_SUCCESS;
}

int
test_ipsec_post_process(struct rte_mbuf *m, const struct ipsec_test_data *td,
			struct ipsec_test_data *res_d, bool silent)
{
	/*
	 * In case of known vector tests & all inbound tests, res_d provided
	 * would be NULL and output data need to be validated against expected.
	 * For inbound, output_text would be plain packet and for outbound
	 * output_text would IPsec packet. Validate by comparing against
	 * known vectors.
	 */
	RTE_SET_USED(res_d);
	return test_ipsec_td_verify(m, td, silent);
}

int
test_ipsec_status_check(struct rte_crypto_op *op,
			enum rte_security_ipsec_sa_direction dir)
{
	int ret = TEST_SUCCESS;

	if (op->status != RTE_CRYPTO_OP_STATUS_SUCCESS) {
		printf("Security op processing failed\n");
		ret = TEST_FAILED;
	}

	RTE_SET_USED(dir);

	return ret;
}