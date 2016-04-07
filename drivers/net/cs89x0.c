#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/irq.h>
//#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
// Added BSt
#include <asm/mach-types.h>
//#ifdef CONFIG_ARCH_SMDK2410
#if 1
//#include "asm/arch-s3c2410/regs-irq.h"
#include "mach/regs-mem.h"
//#include "asm/arch/smdk2410.h"
//#include "net/smdk2410.h"
#endif
#include "cs89x0.h"
//#define FULL_DUPLEX
//#define DEBUG
#define pSMDK2410_ETH_IO         __phys_to_pfn(0x19000000)
#define vSMDK2410_ETH_IO        0xE0000000
#define SMDK2410_EHT_IRQ        IRQ_EINT9 
typedef struct {
    struct net_device_stats stats;
    u16 txlen;
    int char_devnum;
    spinlock_t lock;
} cs8900_t;
int cs89x0_probe (struct net_device *dev);
static struct net_device *cs8900_dev ;
#define MAX_EEPROM_SIZE        256
#define VERSION_STRING  "Cirrus logic cs8900a driver fof linux smdk2410(cpw806@qq.com)"
static inline u16 cs8900_read (struct net_device *dev,u16 reg)
{
    outw (reg,dev->base_addr + PP_Address);
    return (inw (dev->base_addr + PP_Data));
}
static inline void cs8900_write (struct net_device *dev,u16 reg,u16 value)
{
    outw (reg,dev->base_addr + PP_Address);
    outw (value,dev->base_addr + PP_Data);
}
static inline void cs8900_set (struct net_device *dev,u16 reg,u16 value)
{
    cs8900_write (dev,reg,cs8900_read (dev,reg) | value);
}
static inline void cs8900_clear (struct net_device *dev,u16 reg,u16 value)
{
    cs8900_write (dev,reg,cs8900_read (dev,reg) & ~value);
}
static inline void cs8900_frame_read (struct net_device *dev,struct sk_buff *skb,u16 length)
{
    insw (dev->base_addr,skb_put (skb,length),(length + 1) / 2);
}
static inline void cs8900_frame_write (struct net_device *dev,struct sk_buff *skb)
{
    outsw (dev->base_addr,skb->data,(skb->len + 1) / 2);
}

#ifdef DEBUG

static inline int printable (int c)
{
    return ((c >= 32 && c <= 126) ||
            (c >= 174 && c <= 223) ||
            (c >= 242 && c <= 243) ||
            (c >= 252 && c <= 253));
}

static void dump16 (struct net_device *dev,const u8 *s,size_t len)
{
    int i;
    char str[128];
    if (!len) return;
    *str = '\0';
    for (i = 0; i < len; i++) {
        if (i && !(i % 4)) strcat (str," ");
        sprintf (str,"%s%.2x ",str,s[i]);
    }
    for ( ; i < 16; i++) {
        if (i && !(i % 4)) strcat (str," ");
        strcat (str," ");
    }
    strcat (str," ");
    for (i = 0; i < len; i++) sprintf (str,"%s%c",str,printable (s[i]) ? s[i] : '.');
    printk (KERN_DEBUG "%s: %s\n",dev->name,str);
}

static void hexdump (struct net_device *dev,const void *ptr,size_t size)
{
    const u8 *s = (u8 *) ptr;
    int i;
    for (i = 0; i < size / 16; i++, s += 16) dump16 (dev,s,16);
    dump16 (dev,s,size % 16);
}

static void dump_packet (struct net_device *dev,struct sk_buff *skb,const char *type)
{
    printk (KERN_INFO "%s: %s %d byte frame %.2x:%.2x:%.2x:%.2x:%.2x:%.2x to %.2x:%.2x:%.2x:%.2x:%.2x:%.2x type %.4x\n",
            dev->name,
            type,
            skb->len,
            skb->data[0],skb->data[1],skb->data[2],skb->data[3],skb->data[4],skb->data[5],
            skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11],
            (skb->data[12] << 8) | skb->data[13]);
    if (skb->len < 0x100) hexdump (dev,skb->data,skb->len);
}

#endif   
static void cs8900_receive (struct net_device *dev)
{
    //cs8900_t *priv = (cs8900_t *) dev->priv;
    cs8900_t *priv = (cs8900_t *)netdev_priv(dev);
    struct sk_buff *skb;
    u16 status,length;
    status = cs8900_read (dev,PP_RxStatus);
    length = cs8900_read (dev,PP_RxLength);
    if (!(status & RxOK)) {
        priv->stats.rx_errors++;
        if ((status & (Runt | Extradata))) priv->stats.rx_length_errors++;
        if ((status & CRCerror)) priv->stats.rx_crc_errors++;
        return;
    }
    if ((skb = dev_alloc_skb (length + 4)) == NULL) {
        priv->stats.rx_dropped++;
        return;
    }
    skb->dev = dev;
    skb_reserve (skb,2);
    cs8900_frame_read (dev,skb,length);
#ifdef FULL_DUPLEX
    dump_packet (dev,skb,"recv");
#endif   
    skb->protocol = eth_type_trans (skb,dev);
    netif_rx (skb);
    dev->last_rx = jiffies;
    priv->stats.rx_packets++;
    priv->stats.rx_bytes += length;
}
 
static int cs8900_send_start (struct sk_buff *skb,struct net_device *dev)
{
    //cs8900_t *priv = (cs8900_t *) dev->priv;
    cs8900_t *priv = (cs8900_t *)netdev_priv(dev);
    u16 status;
    spin_lock_irq(&priv->lock);
    netif_stop_queue (dev);
    cs8900_write (dev,PP_TxCMD,TxStart (After5));
    cs8900_write (dev,PP_TxLength,skb->len);
    status = cs8900_read (dev,PP_BusST);
    if ((status & TxBidErr)) {
        spin_unlock_irq(&priv->lock);
        printk (KERN_WARNING "%s: Invalid frame size %d!\n",dev->name,skb->len);
        priv->stats.tx_errors++;
        priv->stats.tx_aborted_errors++;
        priv->txlen = 0;
        return (1);
    }
    if (!(status & Rdy4TxNOW)) {
        spin_unlock_irq(&priv->lock);
        printk (KERN_WARNING "%s: Transmit buffer not free!\n",dev->name);
        priv->stats.tx_errors++;
        priv->txlen = 0;
        return (1);
    }
    cs8900_frame_write (dev,skb);
    spin_unlock_irq(&priv->lock);

#ifdef DEBUG
    dump_packet (dev,skb,"send");
#endif   
    dev->trans_start = jiffies;
    dev_kfree_skb (skb);
    priv->txlen = skb->len;
    return (0);
}

static irqreturn_t cs8900_interrupt (int irq,void *id)
{
    struct net_device *dev = (struct net_device *) id;
    cs8900_t *priv;
    volatile u16 status;
    irqreturn_t handled = 0;
#if 0
    if (dev->priv == NULL) {
        printk (KERN_WARNING "%s: irq %d for unknown device.\n",dev->name,irq);
        return 0;
    }
#endif
    //priv = (cs8900_t *) dev->priv;
    priv = (cs8900_t *)netdev_priv(dev);
    while ((status = cs8900_read (dev, PP_ISQ))) {
        handled = 1;
        switch (RegNum (status)) {
        case RxEvent:
            cs8900_receive (dev);
            break;
        case TxEvent:
            priv->stats.collisions += ColCount (cs8900_read (dev,PP_TxCOL));
            if (!(RegContent (status) & TxOK)) {
                priv->stats.tx_errors++;
                if ((RegContent (status) & Out_of_window)) priv->stats.tx_window_errors++;
                if ((RegContent (status) & Jabber)) priv->stats.tx_aborted_errors++;
                break;
            } else if (priv->txlen) {
                priv->stats.tx_packets++;
                priv->stats.tx_bytes += priv->txlen;
            }
            priv->txlen = 0;
            netif_wake_queue (dev);
            break;
        case BufEvent:
            if ((RegContent (status) & RxMiss)) {
                u16 missed = MissCount (cs8900_read (dev,PP_RxMISS));
                priv->stats.rx_errors += missed;
                priv->stats.rx_missed_errors += missed;
            }
            if ((RegContent (status) & TxUnderrun)) {
                priv->stats.tx_errors++;
                priv->stats.tx_fifo_errors++;
                priv->txlen = 0;
                netif_wake_queue (dev);
            }
            break;
        case TxCOL:
            priv->stats.collisions += ColCount (cs8900_read (dev,PP_TxCOL));
            break;
        case RxMISS:
            status = MissCount (cs8900_read (dev,PP_RxMISS));
            priv->stats.rx_errors += status;
            priv->stats.rx_missed_errors += status;
            break;
        }
    }
    return IRQ_RETVAL(handled);
}

static void cs8900_transmit_timeout (struct net_device *dev)
{
    //cs8900_t *priv = (cs8900_t *) dev->priv;
    cs8900_t *priv = (cs8900_t *)netdev_priv(dev);
    priv->stats.tx_errors++;
    priv->stats.tx_heartbeat_errors++;
    priv->txlen = 0;
    netif_wake_queue (dev);
}

static int cs8900_start (struct net_device *dev)
{
    int result;
    //set_irq_type(dev->irq, IRQT_RISING);
    set_irq_type(dev->irq, IRQ_TYPE_EDGE_RISING);
    cs8900_set (dev,PP_RxCFG,RxOKiE | BufferCRC | CRCerroriE | RuntiE | ExtradataiE);
    cs8900_set (dev,PP_RxCTL,RxOKA | IndividualA | BroadcastA);
    cs8900_set (dev,PP_TxCFG,TxOKiE | Out_of_windowiE | JabberiE);
    cs8900_set (dev,PP_BufCFG,Rdy4TxiE | RxMissiE | TxUnderruniE | TxColOvfiE | MissOvfloiE);
    cs8900_set (dev,PP_LineCTL,SerRxON | SerTxON);
    cs8900_set (dev,PP_BusCTL,EnableRQ);
#ifdef FULL_DUPLEX
    cs8900_set (dev,PP_TestCTL,FDX);
#endif   
    udelay(200);   
    if ((result = request_irq (dev->irq, &cs8900_interrupt, 0, dev->name, dev)) <0) {
        printk (KERN_ERR "%s: could not register interrupt %d\n",dev->name, dev->irq);
        return (result);
    }
    netif_start_queue (dev);
    return (0);
}
static int cs8900_stop (struct net_device *dev)
{
    cs8900_write (dev,PP_BusCTL,0);
    cs8900_write (dev,PP_TestCTL,0);
    cs8900_write (dev,PP_SelfCTL,0);
    cs8900_write (dev,PP_LineCTL,0);
    cs8900_write (dev,PP_BufCFG,0);
    cs8900_write (dev,PP_TxCFG,0);
    cs8900_write (dev,PP_RxCTL,0);
    cs8900_write (dev,PP_RxCFG,0);
    free_irq (dev->irq,dev);
    netif_stop_queue (dev);
    return (0);
}

static struct net_device_stats *cs8900_get_stats (struct net_device *dev)
{
   //cs8900_t *priv = (cs8900_t *) dev->priv;
    cs8900_t *priv = (cs8900_t *)netdev_priv(dev);
    return (&priv->stats);
}

static void cs8900_set_receive_mode (struct net_device *dev)
{
    if ((dev->flags & IFF_PROMISC))
        cs8900_set (dev,PP_RxCTL,PromiscuousA);
    else
        cs8900_clear (dev,PP_RxCTL,PromiscuousA);
    //if ((dev->flags & IFF_ALLMULTI) && dev->mc_list)
    if ((dev->flags & IFF_ALLMULTI))// && !netdev_mc_empty(dev))
        cs8900_set (dev,PP_RxCTL,MulticastA);
    else
        cs8900_clear (dev,PP_RxCTL,MulticastA);
}

int cs89x0_probe (struct net_device *dev)
{
    static cs8900_t *priv;
    int i,result;
    u16 value;
    printk (VERSION_STRING"\n");
//    memset (&priv,0,sizeof (cs8900_t));
    //+kevin
    __raw_writel(0x2211d110,S3C2410_BWSCON);
    __raw_writel(0x1f7c,S3C2410_BANKCON3);
//#if defined(CONFIG_ARCH_SMDK2410)
#if 1
    dev->dev_addr[0] = 0x00;
    dev->dev_addr[1] = 0x00;
    dev->dev_addr[2] = 0x3e;
    dev->dev_addr[3] = 0x26;
    dev->dev_addr[4] = 0x0a;
    dev->dev_addr[5] = 0x00;
#endif
    dev->if_port = IF_PORT_10BASET;
    //dev->priv = (void *) &priv;
    priv = (cs8900_t *)netdev_priv(dev);
    spin_lock_init(&priv->lock);
    //hejiasheng SET_MODULE_OWNER (dev);
//#if defined(CONFIG_ARCH_SMDK2410)
#if 1
    dev->base_addr = vSMDK2410_ETH_IO + 0x300;
    dev->irq = IRQ_EINT9;
    printk("debug:[%s-%d]\n", __func__, __LINE__);
#endif
    if ((result = check_mem_region (dev->base_addr, 16))) {
        printk (KERN_ERR "%s: can't get I/O port address 0x%lx\n",dev->name,dev->base_addr);
        return (result);
    }
    request_mem_region (dev->base_addr, 16, dev->name);
    printk("debug:[%s-%d]\n", __func__, __LINE__);
    if ((value = cs8900_read (dev,PP_ProductID)) != EISA_REG_CODE) {
        printk (KERN_ERR "%s: incorrect signature 0x%.4x\n",dev->name,value);
        return (-ENXIO);
    }
    printk("debug:[%s-%d]\n", __func__, __LINE__);
    value = cs8900_read (dev,PP_ProductID + 2);
    if (VERSION (value) != CS8900A) {
        printk (KERN_ERR "%s: unknown chip version 0x%.8x\n",dev->name,VERSION (value));
        return (-ENXIO);
    }
    cs8900_write (dev,PP_IntNum,0);
    printk (KERN_INFO "%s: CS8900A rev %c at %#lx irq=%d",
        dev->name,'B' + REVISION (value) - REV_B, dev->base_addr, dev->irq);
    for (i = 0; i < ETH_ALEN; i += 2)
        cs8900_write (dev,PP_IA + i,dev->dev_addr[i] | (dev->dev_addr[i + 1] << 8));
    printk (", addr:");
    for (i = 0; i < ETH_ALEN; i += 2)
    {
        u16 mac = cs8900_read (dev,PP_IA + i);
        printk ("%cX:%2X", (i==0)?' ':':', mac & 0xff, (mac >> 8));
    }
    printk ("\n");
    return (0);
}
static const struct net_device_ops net_ops = {
    .ndo_init = cs89x0_probe,
    .ndo_open        = cs8900_start,
    .ndo_stop        = cs8900_stop,
    .ndo_tx_timeout        = cs8900_transmit_timeout,
    .ndo_start_xmit     = cs8900_send_start,
    .ndo_get_stats        = cs8900_get_stats,
    .ndo_set_multicast_list = cs8900_set_receive_mode,
#if 0
    .ndo_set_mac_address     = set_mac_address,
    .ndo_poll_controller    = net_poll_controller,
    .ndo_change_mtu        = eth_change_mtu,
    .ndo_validate_addr    = eth_validate_addr,
#endif
};
 
static int __init cs8900_init (void)
{
    struct net_device *ndev;
    ndev = alloc_etherdev(sizeof (cs8900_t));
    if (!ndev) {
        printk("%s: could not allocate device.\n", "cs8900");
        return -ENOMEM;
    }
    printk("allocate cs8900 device ok.\n");
    cs8900_dev = ndev;
    //cs8900_dev->init = cs89x0_probe;
    ether_setup (ndev);
#if 0
    ndev->open = cs8900_start;
    ndev->stop = cs8900_stop;
    ndev->hard_start_xmit = cs8900_send_start;
    ndev->get_stats = cs8900_get_stats;
    ndev->set_multicast_list = cs8900_set_receive_mode;
    ndev->tx_timeout = cs8900_transmit_timeout;
#endif
    ndev->netdev_ops = &net_ops;
    ndev->watchdog_timeo = HZ;
    return (register_netdev (cs8900_dev));
}

static void __exit cs8900_cleanup (void)
{
//    cs8900_t *priv = (cs8900_t *) cs8900_dev->priv;
    cs8900_t *priv = (cs8900_t *)netdev_priv(cs8900_dev);
    if( priv->char_devnum)
    {
//        unregister_chrdev(priv->char_devnum,"cs8900_eeprom");
    }
    release_mem_region (cs8900_dev->base_addr,16);
    unregister_netdev (cs8900_dev);
}
MODULE_AUTHOR ("Abraham van der Merwe <abraham at 2d3d.co.za>");
MODULE_DESCRIPTION (VERSION_STRING);
MODULE_LICENSE ("GPL");
module_init (cs8900_init);
module_exit (cs8900_cleanup);
