#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
    
  acquire(&e1000_lock);  // 获取锁
  
  // 获取当前 TX 环的索引
  uint32 tdt = regs[E1000_TDT];
  
  // 检查环是否已满 - 如果 DD 位没有设置，说明之前的传输还没完成
  if((tx_ring[tdt].status & E1000_TXD_STAT_DD) == 0) {
    // 环已满，返回错误
    release(&e1000_lock);  // 释放锁
    return -1;
  }
  
  // 释放之前在这个描述符位置传输的 mbuf（如果有的话）
  if(tx_mbufs[tdt] != 0) {
    mbuffree(tx_mbufs[tdt]);
  }
  
  // 填充描述符
  tx_ring[tdt].addr = (uint64)m->head;  // 数据包内容的地址
  tx_ring[tdt].length = m->len;         // 数据包长度
  tx_ring[tdt].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;  // 设置必要的命令标志
  
  // 保存 mbuf 指针，以便稍后释放
  tx_mbufs[tdt] = m;
  
  // 更新环位置
  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;
  
  release(&e1000_lock);  // 释放锁
  
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // 计算下一个待处理的接收包索引
  uint32 rdt = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  
  // 处理所有可用的包
  while(1) {
    // 检查是否有新包可用，如果没有新包，停止处理
    if((rx_ring[rdt].status & E1000_RXD_STAT_DD) == 0) {
      break;
    }
    // 检查数据包的长度是否超过了mbuf 的最大大小
    if (rx_ring[rdt].length > MBUF_SIZE) {
    panic("MBUF_SIZE OVERFLOW!");
    }
    
    // 获取当前 mbuf
    struct mbuf *m = rx_mbufs[rdt];
    
    // 更新 mbuf 的长度为描述符中报告的长度
    m->len = rx_ring[rdt].length;
    
    // 将 mbuf 传递给网络栈
    net_rx(m);
    
    // 分配新的 mbuf 来替换刚刚传递的那个
    struct mbuf *new_m = mbufalloc(0);
    if(new_m == 0) {
      // 检查内存分配是否失败
      panic("e1000_recv: mbufalloc failed");
    }
    
    // 将新 mbuf 的数据指针编程到描述符中
    rx_ring[rdt].addr = (uint64)new_m->head;
    rx_ring[rdt].status = 0;  // 清除状态位
    
    // 保存新的 mbuf 指针
    rx_mbufs[rdt] = new_m;
    
    // 移动到下一个描述符
    rdt = (rdt + 1) % RX_RING_SIZE;
  }
  
  // 更新 RDT 寄存器为最后处理的描述符索引
  if(rdt == 0) {
    regs[E1000_RDT] = RX_RING_SIZE - 1;
  } else {
    regs[E1000_RDT] = rdt - 1;
  }

}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
