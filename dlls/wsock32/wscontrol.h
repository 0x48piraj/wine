/* wscontrol.h
 *
 * This header file includes #defines, structure and type definitions,
 * and function declarations that support the implementation of the
 * (undocumented) Winsock 1 call WsControl.
 *
 * The functionality of WsControl was created by observing its behaviour
 * in Windows 98, so there are likely to be bugs with the assumptions
 * that were made.
 *
 * Copyright 2000 James Hatheway
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef WSCONTROL_H_INCLUDED
#define WSCONTROL_H_INCLUDED

typedef unsigned char uchar; /* This doesn't seem to be in any standard headers */

#define WSCTL_SUCCESS        0

/*
 *      TCP/IP action codes.
 */
#define WSCNTL_TCPIP_QUERY_INFO             0x00000000
#define WSCNTL_TCPIP_SET_INFO               0x00000001
#define WSCNTL_TCPIP_ICMP_ECHO              0x00000002
#define WSCNTL_TCPIP_TEST                   0x00000003


/* Structure of an entity ID */
typedef struct TDIEntityID
{
   unsigned long tei_entity;
   unsigned long tei_instance;
} TDIEntityID;

/* Structure of an object ID */
typedef struct TDIObjectID
{
   TDIEntityID   toi_entity;
   unsigned long toi_class;
   unsigned long toi_type;
   unsigned long toi_id;
} TDIObjectID;

typedef struct IPSNMPInfo
{
   unsigned long  ipsi_forwarding;
   unsigned long  ipsi_defaultttl;
   unsigned long  ipsi_inreceives;
   unsigned long  ipsi_inhdrerrors;
   unsigned long  ipsi_inaddrerrors;
   unsigned long  ipsi_forwdatagrams;
   unsigned long  ipsi_inunknownprotos;
   unsigned long  ipsi_indiscards;
   unsigned long  ipsi_indelivers;
   unsigned long  ipsi_outrequests;
   unsigned long  ipsi_routingdiscards;
   unsigned long  ipsi_outdiscards;
   unsigned long  ipsi_outnoroutes;
   unsigned long  ipsi_reasmtimeout;
   unsigned long  ipsi_reasmreqds;
   unsigned long  ipsi_reasmoks;
   unsigned long  ipsi_reasmfails;
   unsigned long  ipsi_fragoks;
   unsigned long  ipsi_fragfails;
   unsigned long  ipsi_fragcreates;
   unsigned long  ipsi_numif;
   unsigned long  ipsi_numaddr;
   unsigned long  ipsi_numroutes;
} IPSNMPInfo;

typedef struct IPAddrEntry
{
   unsigned long  iae_addr;
   unsigned long  iae_index;
   unsigned long  iae_mask;
   unsigned long  iae_bcastaddr;
   unsigned long  iae_reasmsize;
   ushort         iae_context;
   ushort         iae_pad;
} IPAddrEntry;

#ifdef if_type
#undef if_type
#endif
#ifdef if_mtu
#undef if_mtu
#endif
#ifdef if_lastchange
#undef if_lastchange
#endif

#define	MAX_PHYSADDR_SIZE    8
#define	MAX_IFDESCR_LEN      256
typedef struct IFEntry
{
   unsigned long if_index;
   unsigned long if_type;
   unsigned long if_mtu;
   unsigned long if_speed;
   unsigned long if_physaddrlen;
   uchar         if_physaddr[MAX_PHYSADDR_SIZE];
   unsigned long if_adminstatus;
   unsigned long if_operstatus;
   unsigned long if_lastchange;
   unsigned long if_inoctets;
   unsigned long if_inucastpkts;
   unsigned long if_innucastpkts;
   unsigned long if_indiscards;
   unsigned long if_inerrors;
   unsigned long if_inunknownprotos;
   unsigned long if_outoctets;
   unsigned long if_outucastpkts;
   unsigned long if_outnucastpkts;
   unsigned long if_outdiscards;
   unsigned long if_outerrors;
   unsigned long if_outqlen;
   unsigned long if_descrlen;
   uchar         if_descr[1];
} IFEntry;


/* FIXME: real name and definition of this struct that contains
 * an IP route table entry is unknown */
typedef struct IPRouteEntry {
   unsigned long ire_addr;
   unsigned long ire_index;  /*matches if_index in IFEntry and iae_index in IPAddrEntry */
   unsigned long ire_metric;
   unsigned long ire_option4;
   unsigned long ire_option5;
   unsigned long ire_option6;
   unsigned long ire_gw;
   unsigned long ire_option8;
   unsigned long ire_option9;
   unsigned long ire_option10;
   unsigned long ire_mask;
   unsigned long ire_option12;
} IPRouteEntry;


/* Not sure what EXACTLY most of this stuff does.
   WsControl was implemented mainly by observing
   its behaviour in Win98 ************************/
#define	INFO_CLASS_GENERIC         0x100
#define	INFO_CLASS_PROTOCOL        0x200
#define	INFO_TYPE_PROVIDER         0x100
#define	ENTITY_LIST_ID             0
#define	CL_NL_ENTITY               0x301
#define	IF_ENTITY                  0x200
#define	ENTITY_TYPE_ID             1
#define	IP_MIB_ADDRTABLE_ENTRY_ID  0x102
#define	IP_MIB_ROUTETABLE_ENTRY_ID 0x101	/* FIXME: not real name */
/************************************************/

/* Valid values to get back from entity type ID query */
#define	CO_TL_NBF   0x400    /* Entity implements NBF prot. */
#define	CO_TL_SPX   0x402    /* Entity implements SPX prot. */
#define	CO_TL_TCP   0x404    /* Entity implements TCP prot. */
#define	CO_TL_SPP   0x406    /* Entity implements SPP prot. */
#define	CL_TL_NBF   0x401    /* CL NBF protocol */
#define	CL_TL_UDP   0x403    /* Entity implements UDP */
#define	ER_ICMP     0x380    /* The ICMP protocol */
#define	CL_NL_IPX   0x301    /* Entity implements IPX */
#define	CL_NL_IP    0x303    /* Entity implements IP */
#define	AT_ARP      0x280    /* Entity implements ARP */
#define	AT_NULL     0x282    /* Entity does no address */
#define	IF_GENERIC  0x200    /* Generic interface */
#define	IF_MIB      0x202    /* Supports MIB-2 interface */


#endif /* WSCONTROL_H_INCLUDED */
