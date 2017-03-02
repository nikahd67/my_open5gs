#define TRACE_MODULE _s6a_hss_init

#include "core_debug.h"
#include "core_pool.h"
#include "core_lib.h"

#include "hss_ctx.h"
#include "s6a_app.h"

static struct disp_hdl *hss_hdl_fb = NULL; /* handler for fallback cb */
static struct disp_hdl *hss_hdl_tr = NULL; /* handler for Test-Request req cb */

/* Default callback for the application. */
static int hss_fb_cb(struct msg **msg, struct avp *avp, 
        struct session *sess, void *opaque, enum disp_action *act)
{
	/* This CB should never be called */
	d_warn("Unexpected message received!");
	
	return ENOTSUP;
}

/* Callback for incoming Test-Request messages */
static int hss_air_cb( struct msg **msg, struct avp *avp, 
        struct session *sess, void *opaque, enum disp_action *act)
{
	struct msg *ans, *qry;
    struct avp *avpch1, *avpch2;
    union avp_value val;
	
	if (msg == NULL)
		return EINVAL;
	
	/* Create answer header */
	qry = *msg;
	d_assert(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, msg, 0) == 0, 
            return -1,);
	ans = *msg;
	
	/* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
	d_assert(fd_msg_rescode_set(ans, "DIAMETER_SUCCESS", NULL, NULL, 1) == 0,
            goto out,);

    /* Set the Auth-Session-Statee AVP */
    d_assert(fd_msg_avp_new(s6a_auth_session_state, 0, &avp) == 0, goto out,);
    val.i32 = 1;
    d_assert(fd_msg_avp_setvalue(avp, &val) == 0, goto out,);
    d_assert(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, avp) == 0, goto out,);

    /* Set the Authentication-Info */
    d_assert(fd_msg_avp_new(s6a_authentication_info, 0, &avp) == 0, goto out,);
    d_assert(fd_msg_avp_new(s6a_e_utran_vector, 0, &avpch1) == 0, goto out,);

    #define TEST_RAND "RAND_123456"
    d_assert(fd_msg_avp_new(s6a_rand, 0, &avpch2) == 0, goto out,);
    val.os.data = (unsigned char*)TEST_RAND;
    val.os.len = strlen(TEST_RAND);
    d_assert(fd_msg_avp_setvalue(avpch2, &val) == 0, goto out,);
    d_assert(fd_msg_avp_add(avpch1, MSG_BRW_LAST_CHILD, avpch2) == 0, 
            goto out,);

    #define TEST_XRES "XRES_123456"
    d_assert(fd_msg_avp_new(s6a_xres, 0, &avpch2) == 0, goto out,);
    val.os.data = (unsigned char*)TEST_XRES;
    val.os.len = strlen(TEST_XRES);
    d_assert(fd_msg_avp_setvalue(avpch2, &val) == 0, goto out,);
    d_assert(fd_msg_avp_add(avpch1, MSG_BRW_LAST_CHILD, avpch2) == 0,
            goto out,);

    #define TEST_AUTH "AUTH_123456"
    d_assert(fd_msg_avp_new(s6a_autn, 0, &avpch2) == 0, goto out,);
    val.os.data = (unsigned char*)TEST_AUTH;
    val.os.len = strlen(TEST_AUTH);
    d_assert(fd_msg_avp_setvalue(avpch2, &val) == 0, goto out,);
    d_assert(fd_msg_avp_add(avpch1, MSG_BRW_LAST_CHILD, avpch2) == 0,
            goto out,);

    #define TEST_KASME "KASME_123456"
    d_assert(fd_msg_avp_new(s6a_kasme, 0, &avpch2) == 0, goto out,);
    val.os.data = (unsigned char*)TEST_KASME;
    val.os.len = strlen(TEST_KASME);
    d_assert(fd_msg_avp_setvalue(avpch2, &val) == 0, goto out,);
    d_assert(fd_msg_avp_add(avpch1, MSG_BRW_LAST_CHILD, avpch2) == 0, 
            goto out,);

    d_assert(fd_msg_avp_add(avp, MSG_BRW_LAST_CHILD, avpch1) == 0, goto out,);
    d_assert(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, avp) == 0, goto out,);
	
	/* Send the answer */
	d_assert(fd_msg_send(msg, NULL, NULL) == 0, goto out,);
	
	/* Add this value to the stats */
	d_assert(pthread_mutex_lock(&s6a_config->stats_lock) == 0,,) ;
	s6a_config->stats.nb_echoed++;
	d_assert(pthread_mutex_unlock(&s6a_config->stats_lock) == 0,,);

    d_assert(fd_msg_free(qry) == 0,,);

	return 0;
out:
    d_assert(fd_msg_free(ans) == 0,,);
	
    return -1;
}

int hss_init(void)
{
	struct disp_when data;

    hss_ctx_init();

    /* FIXME : this is a sample UE for testing */
    {
        ue_ctx_t *ue;
        char buffer[MAX_KEY_LEN];

        #define K "465B5CE8B199B49FAA5F0A2EE238A6BC"
        #define OP "5F1D289C5D354D0A140C2548F5F3E3BA"
        #define OPc "E8ED289DEBA952E4283B54E88E6183CA"
        #define AMF "8000"

        ue = hss_ue_ctx_add();
        d_assert(ue, return -1, "UE context add failed");

        #define UE1_IMSI "001010123456800"
        strcpy((char*)ue->imsi, UE1_IMSI);
        ue->imsi_len = strlen(UE1_IMSI);

        memcpy(ue->k, core_ascii_to_hex(K, strlen(K), buffer), MAX_KEY_LEN);
        memcpy(ue->op, core_ascii_to_hex(OP, strlen(OP), buffer), MAX_KEY_LEN);
        memcpy(ue->opc, 
            core_ascii_to_hex(OPc, strlen(OPc), buffer), MAX_KEY_LEN);
        memcpy(ue->amf, 
            core_ascii_to_hex(AMF, strlen(AMF), buffer), MAX_KEY_LEN);

        ue = hss_ue_ctx_add();
        d_assert(ue, return -1, "UE context add failed");

        #define UE2_IMSI "001010123456796"
        strcpy((char*)ue->imsi, UE2_IMSI);
        ue->imsi_len = strlen(UE2_IMSI);

        memcpy(ue->k, core_ascii_to_hex(K, strlen(K), buffer), MAX_KEY_LEN);
        memcpy(ue->op, core_ascii_to_hex(OP, strlen(OP), buffer), MAX_KEY_LEN);
        memcpy(ue->opc, 
            core_ascii_to_hex(OPc, strlen(OPc), buffer), MAX_KEY_LEN);
        memcpy(ue->amf, 
            core_ascii_to_hex(AMF, strlen(AMF), buffer), MAX_KEY_LEN);
    }

	memset(&data, 0, sizeof(data));
	data.app = s6a_appli;
	data.command = s6a_cmd_air;
	
	/* fallback CB if command != unexpected message received */
	d_assert(fd_disp_register(hss_fb_cb, DISP_HOW_APPID, &data, NULL, 
                &hss_hdl_fb) == 0, return -1,);
	
	/* Now specific handler for Authentication-Information-Request */
	d_assert(fd_disp_register(hss_air_cb, DISP_HOW_CC, &data, NULL, 
                &hss_hdl_tr) == 0, return -1,);
	
	return 0;
}

void hss_final(void)
{
	if (hss_hdl_fb) {
		(void) fd_disp_unregister(&hss_hdl_fb, NULL);
	}
	if (hss_hdl_tr) {
		(void) fd_disp_unregister(&hss_hdl_tr, NULL);
	}

    /* FIXME : this is a sample UE for testing */
    {
        hss_ue_ctx_remove_all();
    }

    hss_ctx_final();
	
	return;
}
