
#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_udp.h>
#include <rte_ip.h>
#include <rte_ethdev.h>

#include "hanr_client.h"
#include "hanr_msg.h"
#include "T0_module/T0_module_plugins.h"
#include "T1_module/T1_module_plugins.h"
#include "T2_module/T2_module_plugins.h"

static inline int hanr_client_process_register_request(struct hanr_msg_register_request *msg)
{
    if (hanr_conf.verbose) {
        hanr_dump_register_request(msg);
    }

    return 0;
}

static inline int hanr_client_process_register_reply(struct hanr_msg_register_reply *msg)
{
    if (hanr_conf.verbose) {
        hanr_dump_register_reply(msg);
    }

    return 0;
}

static inline int hanr_client_process_withdraw_request(struct hanr_msg_withdraw_request *msg)
{
    if (hanr_conf.verbose) {
        hanr_dump_withdraw_request(msg);
    }

    return 0;
}

static inline int hanr_client_process_withdraw_reply(struct hanr_msg_withdraw_reply *msg)
{
    if (hanr_conf.verbose) {
        hanr_dump_withdraw_reply(msg);
    }

    return 0;
}

static inline int hanr_client_process_query_request(struct hanr_client_conf *conf, struct hanr_packet *pkt, struct hanr_msg_query_request *msg)
{
    int ret=0;
    if (hanr_conf.verbose) {
        printf("start process: %d\n", msg->type);
        hanr_dump_query_request(msg);
    }
    printf("process in: %d\n", conf->mode);
    switch (conf->mode) {
        case 0:
            ret = hanr_t0_do_query(conf, pkt , msg);
            break;
        case 1:
            ret = hanr_t1_do_query(conf, pkt , msg);
            break;
        case 2:
            ret = hanr_t2_do_query(conf, pkt , msg);
            break;
        default:
            printf("unknow plugins to process: %d\n", conf->mode);
    }

    if(ret <= 0){
        printf("process failed and request_id: %d\n",msg->request_id);
    }
    return 0;
}

static inline int hanr_client_process_query_reply(struct hanr_msg_query_reply *msg)
{
    if (hanr_conf.verbose) {
        hanr_dump_query_reply(msg);
    }

    return 0;
}

static int hanr_client_process_msg(struct hanr_client_conf *conf, uint8_t *buf, struct hanr_packet *pkt)
{
    uint8_t type = buf[0];
    switch (type) {
        case HANR_MSG_REGISTER_REQUEST:
            hanr_client_process_register_request((struct hanr_msg_register_request*)buf);
            break;
        case HANR_MSG_REGISTER_REPLY:
            hanr_client_process_register_reply((struct hanr_msg_register_reply*)buf);
            break;
        case HANR_MSG_WITHDRAW_REQUEST:
            hanr_client_process_withdraw_request((struct hanr_msg_withdraw_request*)buf);
            break;
        case HANR_MSG_WITHDRAW_REPLY:
            hanr_client_process_withdraw_reply((struct hanr_msg_withdraw_reply*)buf);
            break;
        case HANR_MSG_QUERY_REQUEST:
            hanr_client_process_query_request(conf, pkt, (struct hanr_msg_query_request*)buf);
            break;
        case HANR_MSG_QUERY_REPLY:
            hanr_client_process_query_reply((struct hanr_msg_query_reply*)buf);
            break;
        default:
            printf("unknow msg type: %d\n", type);
    }

    return 0;
}

int hanr_client_process_rx(struct hanr_client_conf *conf, struct rte_mbuf *mbuf)
{
    int ret = 0;
    struct hanr_packet *pkt = NULL;
    pkt = malloc(sizeof(struct hanr_packet));
    if (pkt == NULL) {
        printf("Malloc memory failed for packet.\n");
        return -1;
    }
    memset(pkt, 0, sizeof(struct hanr_packet));
    // struct rte_ether_hdr* eth_hdr;
    // struct rte_ipv4_hdr* ipv4_hdr;
    // struct rte_ipv6_hdr* ipv6_hdr;
    // struct rte_udp_hdr* udp_hdr = NULL;

    pkt->eth_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr*);

    uint16_t eth_type = rte_be_to_cpu_16(pkt->eth_hdr->ether_type);

    if (eth_type == 0x0800) {
        pkt->ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf,
                        struct rte_ipv4_hdr*,
                        sizeof(struct rte_ether_hdr));
        if (pkt->ipv4_hdr->next_proto_id == IPPROTO_UDP){
            pkt->udp_hdr = (struct rte_udp_hdr*)((unsigned char*)pkt->ipv4_hdr +
                        sizeof(struct rte_ipv4_hdr));
        }
    } else if (eth_type == 0x86DD) {
        pkt->ipv6_hdr = rte_pktmbuf_mtod_offset(mbuf, 
                        struct rte_ipv6_hdr*,
                        sizeof(struct rte_ether_hdr));
        if (pkt->ipv6_hdr->proto == IPPROTO_UDP) {
            pkt->udp_hdr = (struct rte_udp_hdr*)((unsigned char*)pkt->ipv6_hdr +
                        sizeof(struct rte_ipv6_hdr));
        }
    } else {
        //IRS_LOG_ERROR("Unknown packet type: [%d], not IPv4 or IPv6 packet.", mbuf->packet_type);
        return -1;
    }
    
    pkt->mbuf = mbuf;

    if (pkt->udp_hdr != NULL) {
        //irs_client_decode_msg((uint8_t*)(udp_hdr+1));
        hanr_client_process_msg(conf, (uint8_t*)(pkt->udp_hdr + 1), pkt);
    } else {
        //IRS_LOG_DEBUG("Not a udp packet.");
        return -1;
    }

    return 0;
}

static struct rte_mbuf *bufs[RTE_MAX_ETHPORTS][MAX_PKT_BURST];


#define PREFETCH_OFFSET 3

/**
 * @brief RX main
 * @param  arg              My Param doc
 * @return int 
 */
int hanr_rx_main(void *arg)
{
    printf("Start rx on lcore: %d, socket: %d\n", rte_lcore_id(), rte_socket_id());
    int nb_rx;
    int i, j;
    uint32_t lcore_id = rte_lcore_id();
    struct hanr_client_conf *conf = (struct hanr_client_conf*)arg;
    struct hanr_lcore_port_queue *pq = &hanr_conf.rxq[lcore_id];
    uint16_t port_id = pq->port_id;
    uint16_t queue_id = pq->queue_id;
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
    uint64_t count = 0;
    uint64_t pkts = 0;
    uint64_t pkts_diff; 
    uint64_t pkts_last = 0;
    uint64_t start, end, diff;
    printf("polling on port: %d, queue: %d\n", port_id, queue_id);

    start = rte_rdtsc();
    while (!hanr_quit)
    {
        nb_rx = rte_eth_rx_burst(port_id, queue_id,
                                 pkts_burst, MAX_PKT_BURST);
        if (unlikely(nb_rx == 0)) {
            continue;
        }

        //printf("Get packets: %d\n", nb_rx);
        
        for (j = 0; j < PREFETCH_OFFSET && j < nb_rx; j++)
        {
            rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[j], void *));
        }
        for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++)
        {
            rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[j + PREFETCH_OFFSET], void *));
            hanr_client_process_rx(conf, pkts_burst[j]);
        }
        for (; j < nb_rx; j++)
        {
            hanr_client_process_rx(conf, pkts_burst[j]);
            
        }
        
        //printf("Recv: %d packets\n", nb_rx);
        for (i = 0; i < nb_rx; i++)
        {
            rte_pktmbuf_free(pkts_burst[i]);
        }

        count++;
        pkts += nb_rx;
        if (count % 1000000 == 0) {
            end = rte_rdtsc();
            diff = end - start;
            pkts_diff = pkts - pkts_last;
            printf("[%d:%d:%d] Rcvd packets %lu: Total %lu cycles, AVG %f cycles\n", lcore_id, port_id, queue_id, pkts_diff, diff, (double)diff/pkts_diff);
            struct rte_eth_stats stats;
            rte_eth_stats_get(port_id, &stats);
            printf("packets: %lu/%lu, bytes: %lu/%lu, errors: %lu/%lu, imissed: %lu, nombuf: %lu\n",
                stats.ipackets, stats.opackets,
                stats.ibytes, stats.obytes,
                stats.ierrors, stats.oerrors,
                stats.imissed, stats.rx_nombuf);
            pkts_last = pkts;
            start = rte_rdtsc();
        }
    }

    return 0;
}