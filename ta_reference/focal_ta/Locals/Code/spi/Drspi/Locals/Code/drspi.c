#include "drStd.h"
#include "DrApi/DrApiMm.h"
#include "DrApi/DrApiThread.h"

#include "spi_debug.h"
#include "spi_regs.h"
#include "spi.h"

#define SPI_TEST

/*mapping TX/RX DMA phy addr flag*/
#define SPI_TX_DMA_FLAG 1
#define SPI_RX_DMA_FLAG 0

/*base address*/
uint8_t  *SpiBaseVA = 0;
uint8_t  *SpiTxDmaVA = 0;
uint8_t  *SpiRxDmaVA = 0;
uint8_t  *SpiPdnBaseVA = 0;
uint8_t  *SpiCfgBaseVA = 0;
uint8_t  *SpiGpioBaseVA = 0;
uint8_t  *SpiDapcBaseVA = 0;

/*SPI transfer auto select mode*/
//#define SPI_AUTO_SELECT_MODE

/*this is for auto select DMA Or FIFO mode*/
#ifdef SPI_AUTO_SELECT_MODE
#define SPI_DATA_SIZE 16
#endif

static struct mt_chip_conf chip_config_default;
struct mt_chip_conf* chip_config = &chip_config_default;
static struct spi_transfer* xfer = NULL;

struct mt_chip_conf* GetChipConfig(void)
{
	return chip_config;
}	

struct spi_transfer* GetSpiTransfer(void)
{
	return xfer;
}

/*use this flag to sync every data transfer*/
static int32_t irq_flag = IRQ_IDLE;
/*
* 1: wait interrupt to handle
* 2: finish interrupt handle turn to idle state
*/
int32_t GetIrqFlag(void)
{
	//SPI_MSG("GetIrqFlag done\n");
	return irq_flag;
}	

void SetIrqFlag(enum spi_irq_flag flag)
{
	irq_flag = flag;
	//SPI_MSG("SetIrqFlag done\n");
}

static int32_t running = 0;
void SetPauseStatus(int32_t status)
{
	running = status;
}

int32_t GetPauseStatus(void)
{
	return running;
}

void dump_chip_config(struct mt_chip_conf *chip_config)
{
    if(chip_config != NULL) {
	  SPI_MSG("setuptime=%d\n",chip_config->setuptime);
	  SPI_MSG("holdtime=%d\n",chip_config->holdtime);
	  SPI_MSG("high_time=%d\n",chip_config->high_time);
	  SPI_MSG("low_time=%d\n",chip_config->low_time);
	  SPI_MSG("cs_idletime=%d\n",chip_config->cs_idletime);
	  SPI_MSG("ulthgh_thrsh=%d\n",chip_config->ulthgh_thrsh);
	  SPI_MSG("cpol=%d\n",chip_config->cpol);
	  SPI_MSG("cpha=%d\n",chip_config->cpha);
	  SPI_MSG("tx_mlsb=%d\n",chip_config->tx_mlsb);
	  SPI_MSG("rx_mlsb=%d\n",chip_config->rx_mlsb);
	  SPI_MSG("tx_endian=%d\n",chip_config->tx_endian);
	  SPI_MSG("rx_endian=%d\n",chip_config->rx_endian);
	  SPI_MSG("com_mod=%d\n",chip_config->com_mod);
	  SPI_MSG("pause=%d\n",chip_config->pause);
	  SPI_MSG("finish_intr=%d\n",chip_config->finish_intr);
	  SPI_MSG("deassert=%d\n",chip_config->deassert);
	  SPI_MSG("ulthigh=%d\n",chip_config->ulthigh);
	  SPI_MSG("tckdly=%d\n",chip_config->tckdly);
    }else {
	  SPI_MSG("dump chip_config is NULL !!\n");
    }
	
}

void dump_chip_config_default(void)
{
    dump_chip_config(chip_config);
}

void dump_reg_config(void)
{
	SPI_MSG("SPI_REG_CFG0=0x%x\n",SPI_READ(SPI_REG_CFG0));
    SPI_MSG("SPI_REG_CFG1=0x%x\n",SPI_READ(SPI_REG_CFG1));
	SPI_MSG("SPI_REG_TX_SRC=0x%x\n",SPI_READ(SPI_REG_TX_SRC));
	SPI_MSG("SPI_REG_RX_DST=0x%x\n",SPI_READ(SPI_REG_RX_DST));
	//SPI_MSG("SPI_REG_RX_DATA=0x%x\n",SPI_READ(SPI_REG_RX_DATA));
	SPI_MSG("SPI_REG_CMD=0x%x\n",SPI_READ(SPI_REG_CMD));
	//SPI_MSG("SPI_REG_STATUS0=0x%x\n",SPI_READ(SPI_REG_STATUS0));
	//SPI_MSG("SPI_REG_STATUS1=0x%x\n",SPI_READ(SPI_REG_STATUS1));
	SPI_MSG("SPI_REG_PAD_SEL=0x%x\n",SPI_READ(SPI_REG_PAD_SEL));	
	SPI_MSG("----SPI DAPC LOCK STATUS--- :SPI_DAPC_STATUS=0x%x\n", SPI_READ(SPI_DAPC_STATUS) ); //SPI DAPC LOCK
}

static void SpiDMAMapping(uint32_t dma_pa_addr, uint32_t flag)
{
   drApiResult_t ret;
   uint32_t mapFlag = MAP_READABLE | MAP_WRITABLE | /*MAP_ALLOW_NONSECURE |*/ MAP_UNCACHED ;
   uint8_t *temp = 0;
   //drApiMapPhys((addr_t)dma_va_addr,  2048*1024, (addr_t)align_2m(dma_pa_addr), mapFlag); 
   ret = drApiMapPhysicalBuffer((uint64_t)(dma_pa_addr),256*1024,mapFlag,(void **)&temp);
   
   if (E_OK != ret) {
	   drApiLogPrintf("[SpiDMAMapping] drApiMapPhysicalBuffer failed ret=%d\n", ret);
   }
   if(SPI_TX_DMA_FLAG == flag)
   	 SpiTxDmaVA = temp;
   else
   	 SpiRxDmaVA = temp;
   
   SPI_MSG("spi_dma_addr, hasMapped\n");
}

void SpiDMAUnMapping(void)
{
   drApiResult_t ret;
   
   ret = drApiUnmapBuffer(SpiTxDmaVA);
   
   if (E_OK != ret) {
	   drApiLogPrintf("[SpiDMAMapping] drApiMapPhysicalBuffer Tx failed ret=%d\n", ret);
   }
   ret = drApiUnmapBuffer(SpiRxDmaVA);
   
   if (E_OK != ret) {
	   drApiLogPrintf("[SpiDMAMapping] drApiMapPhysicalBuffer Tx failed ret=%d\n", ret);
   }
   
   SPI_MSG("spi_dma_addr, hasUnMapped\n");
}

static uint32_t IsInterruptEnable(void)
{
	uint32_t cmd;
	cmd = SPI_READ(SPI_REG_CMD);
	return (cmd >> SPI_CMD_FINISH_IE_OFFSET) & 1;
}

static void inline clear_pause_bit(void)
{
	uint32_t reg_val;
	
	reg_val = SPI_READ(SPI_REG_CMD);
	reg_val &= ~SPI_CMD_PAUSE_EN_MASK;
	SPI_WRITE(SPI_REG_CMD, reg_val);

	return;
}


static void inline SetPauseBit(void)
{
	uint32_t reg_val;
	
	reg_val = SPI_READ(SPI_REG_CMD);
	reg_val |= 1 << SPI_CMD_PAUSE_EN_OFFSET;	
	SPI_WRITE(SPI_REG_CMD, reg_val);
	return;
}

static void inline ClearResumeBit(void)
{
	uint32_t reg_val;

	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~SPI_CMD_RESUME_MASK;
	SPI_WRITE ( SPI_REG_CMD, reg_val );

	return;
}

/*
*  SpiSetupPacket: used to define per data length and loop count
* @ ptr : data structure from User
*/
static uint32_t inline SpiSetupPacket(struct spi_transfer *ptr)
{ 
	uint32_t  packet_size,packet_loop, cfg1;

	//set transfer packet and loop		
	if(ptr->len < PACKET_SIZE) 
		packet_size = ptr->len;
	else
		packet_size = PACKET_SIZE;
	
	if(ptr->len % packet_size){
		//packet_loop = ptr->len/packet_size + 1;
		/*parameter not correct, there will be more data transfer,notice user to change*/			
		SPI_MSG("The lens are not a multiple of %d, your len %u\n\n",PACKET_SIZE,ptr->len);
		//return E_INVALID;
	}
		packet_loop = (ptr->len)/packet_size;

	SPI_MSG("The packet_size:0x%x packet_loop:0x%x\n", packet_size,packet_loop);

	cfg1 = SPI_READ(SPI_REG_CFG1);
	cfg1 &= ~(SPI_CFG1_PACKET_LENGTH_MASK + SPI_CFG1_PACKET_LOOP_MASK);
	cfg1 |= (packet_size - 1)<<SPI_CFG1_PACKET_LENGTH_OFFSET;
	cfg1 |= (packet_loop - 1)<<SPI_CFG1_PACKET_LOOP_OFFSET;
	SPI_WRITE ( SPI_REG_CFG1, cfg1 );

	return 0;
}


static void inline SpiStartTransfer(void)
{
	uint32_t reg_val;
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val |= 1 << SPI_CMD_ACT_OFFSET;

	/*All register must be prepared before setting the start bit [SMP]*/
	SPI_WRITE( SPI_REG_CMD, reg_val);	

	return;
}

void SpiSetupTransfer(struct spi_transfer* ptr)
{
	if(NULL == ptr) {
		SPI_ERR("spi_transfer is NULL !!!\n");
	} else {
		xfer = ptr;
	}
}

static int32_t TransferDmaMapping(void)
{	
	/*need implement*/
	return 0;	
}

static void TransferDmaUnmapping(void)
{
    /*need implement*/
	return;
}

/*
*  SpiChipConfig: used to define per data length and loop count
* @ ptr : HW config setting from User
*/
void SpiChipConfig(struct mt_chip_conf *ptr)
{
	if(NULL == ptr) { //default
		chip_config->setuptime = 3;
		chip_config->holdtime = 3;
		chip_config->high_time = 50;
		chip_config->low_time = 50;
		chip_config->cs_idletime = 2;
		chip_config->ulthgh_thrsh = 0;

		chip_config->cpol = 0;
		chip_config->cpha = 0;
		
		chip_config->rx_mlsb = 1; 
		chip_config->tx_mlsb = 1;

		chip_config->tx_endian = 0;
		chip_config->rx_endian = 0;

		chip_config->com_mod = FIFO_TRANSFER;
		chip_config->pause = PAUSE_MODE_ENABLE;
		chip_config->finish_intr = 1;
		chip_config->deassert = 0;
		chip_config->ulthigh = 0;
		chip_config->tckdly = 0;
	}else {
		chip_config->setuptime = ptr->setuptime;
		chip_config->holdtime = ptr->holdtime;
		chip_config->high_time = ptr->high_time;
		chip_config->low_time = ptr->low_time;
		chip_config->cs_idletime = ptr->cs_idletime;
		chip_config->ulthgh_thrsh = ptr->ulthgh_thrsh;

		chip_config->cpol = ptr->cpol;
		chip_config->cpha = ptr->cpha;
		
		chip_config->rx_mlsb = ptr->rx_mlsb; 
		chip_config->tx_mlsb = ptr->tx_mlsb ;

		chip_config->tx_endian = ptr->tx_endian;
		chip_config->rx_endian = ptr->rx_endian;

		chip_config->com_mod = ptr->com_mod;
		chip_config->pause = ptr->pause;
		chip_config->finish_intr = ptr->finish_intr;
		chip_config->deassert = ptr->deassert;
		chip_config->ulthigh = ptr->ulthigh;
		chip_config->tckdly = ptr->tckdly ;

	}
//	spidev->controller_data = chip_config;
	dump_chip_config(chip_config);
}

static void inline SpiDisableDma(void)
{ 
	uint32_t  cmd;

	cmd = SPI_READ( SPI_REG_CMD);
	cmd &= ~SPI_CMD_TX_DMA_MASK;
	cmd &= ~SPI_CMD_RX_DMA_MASK;
	SPI_WRITE(SPI_REG_CMD, cmd);
	
	return ;
}

static void inline SpiEnableDma(struct spi_transfer *xfer,uint32_t mode)
{ 
	uint32_t  cmd;

	cmd = SPI_READ(SPI_REG_CMD);
#define SPI_4B_ALIGN 0x4	
	/*set up the DMA bus address*/
	if((mode == DMA_TRANSFER)){
		if((xfer->tx_buf != NULL)|| ((xfer->tx_dma != INVALID_DMA_ADDRESS) && (xfer->tx_dma != 0))){
			if(xfer->tx_dma & (SPI_4B_ALIGN - 1)){
				SPI_MSG("Warning!Tx_DMA address should be 4Byte alignment,buf:%p,dma:%x\n",xfer->tx_buf,xfer->tx_dma);
			}
			SPI_WRITE(SPI_REG_TX_SRC,(xfer->tx_dma));
			cmd |= 1 << SPI_CMD_TX_DMA_OFFSET;
		}	
		SpiDMAMapping(xfer->tx_dma,SPI_TX_DMA_FLAG);
		drApiLogPrintf("SpiEnableDma, TX [%x]\n", SpiTxDmaVA);
	}
	if((mode == DMA_TRANSFER)){
		if((xfer->rx_buf != NULL)|| ((xfer->rx_dma != INVALID_DMA_ADDRESS) && (xfer->rx_dma != 0))){
			if(xfer->rx_dma & (SPI_4B_ALIGN - 1)){
				SPI_MSG("Warning!Rx_DMA address should be 4Byte alignment,buf:%p,dma:%x\n",xfer->rx_buf,xfer->rx_dma);
			}
			SPI_WRITE(SPI_REG_RX_DST, (xfer->rx_dma));	
			cmd |= 1 << SPI_CMD_RX_DMA_OFFSET;
		}		
		SpiDMAMapping(xfer->rx_dma,SPI_RX_DMA_FLAG);
		SPI_MSG("SpiEnableDma, RX [%x]\n", SpiRxDmaVA);
	}	
	SPI_WRITE(SPI_REG_CMD, cmd);
	
	return ;
}

static void  inline SpiResumeTransfer(void)
{
	uint32_t reg_val;

	reg_val = SPI_READ(SPI_REG_CMD);
	reg_val &= ~SPI_CMD_RESUME_MASK;
	reg_val |= 1 << SPI_CMD_RESUME_OFFSET;
	/*All register must be prepared before setting the start bit [SMP]*/
	SPI_WRITE( SPI_REG_CMD, reg_val );	

	return;	
}


void ResetSpi(void)
{
	uint32_t reg_val;

	/*set the software reset bit in SPI_REG_CMD.*/
	reg_val=SPI_READ(SPI_REG_CMD);
	reg_val &= ~SPI_CMD_RST_MASK;
	reg_val |= 1 << SPI_CMD_RST_OFFSET;
	SPI_WRITE(SPI_REG_CMD, reg_val);
	
	reg_val = SPI_READ(SPI_REG_CMD);
	reg_val &= ~SPI_CMD_RST_MASK;
	SPI_WRITE(SPI_REG_CMD, reg_val);
	SPI_MSG("ResetSpi!\n");

	return;
}

/* 
* Write chip configuration to HW register 
*/
int32_t SpiSetup(struct mt_chip_conf *chip_config)
{
	uint32_t reg_val;
	if(chip_config == NULL) {
       SPI_MSG("SpiSetup chip_config is NULL !!\n");
	}else {
	/*set the timing*/
	reg_val = SPI_READ( SPI_REG_CFG0 );
	reg_val &= ~ ( SPI_CFG0_SCK_HIGH_MASK |SPI_CFG0_SCK_LOW_MASK );
	reg_val &= ~ ( SPI_CFG0_CS_HOLD_MASK |SPI_CFG0_CS_SETUP_MASK );
	reg_val |= ( (chip_config->high_time-1) << SPI_CFG0_SCK_HIGH_OFFSET );
	reg_val |= ( (chip_config->low_time-1) << SPI_CFG0_SCK_LOW_OFFSET );
	reg_val |= ( (chip_config->holdtime-1) << SPI_CFG0_CS_HOLD_OFFSET );
	reg_val |= ( (chip_config->setuptime-1) << SPI_CFG0_CS_SETUP_OFFSET );
	SPI_WRITE( SPI_REG_CFG0, reg_val );
	
	reg_val = SPI_READ ( SPI_REG_CFG1 ); 
	reg_val &= ~(SPI_CFG1_CS_IDLE_MASK);
	reg_val |= ( (chip_config->cs_idletime-1 ) << SPI_CFG1_CS_IDLE_OFFSET );
	
	reg_val &= ~(SPI_CFG1_GET_TICK_DLY_MASK);
	reg_val |= ( ( chip_config->tckdly ) << SPI_CFG1_GET_TICK_DLY_OFFSET );
	SPI_WRITE( SPI_REG_CFG1, reg_val );
	
    /*config CFG1 bit[29:26] is 0*/
	reg_val = SPI_READ ( SPI_REG_CFG1 );
	reg_val &= ~(0xF<<26);
	SPI_WRITE( SPI_REG_CFG1, reg_val );
	
	/*set the mlsbx and mlsbtx*/
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~ ( SPI_CMD_TX_ENDIAN_MASK | SPI_CMD_RX_ENDIAN_MASK );
	reg_val &= ~ ( SPI_CMD_TXMSBF_MASK| SPI_CMD_RXMSBF_MASK );
	reg_val &= ~ ( SPI_CMD_CPHA_MASK | SPI_CMD_CPOL_MASK );
	reg_val |= ( chip_config->tx_mlsb << SPI_CMD_TXMSBF_OFFSET );
	reg_val |= ( chip_config->rx_mlsb << SPI_CMD_RXMSBF_OFFSET );
	reg_val |= (chip_config->tx_endian << SPI_CMD_TX_ENDIAN_OFFSET );
	reg_val |= (chip_config->rx_endian << SPI_CMD_RX_ENDIAN_OFFSET );
	reg_val |= (chip_config->cpha << SPI_CMD_CPHA_OFFSET );
	reg_val |= (chip_config->cpol << SPI_CMD_CPOL_OFFSET );
	SPI_WRITE(SPI_REG_CMD, reg_val);

	/*set pause mode*/
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~SPI_CMD_PAUSE_EN_MASK;
	reg_val &= ~ SPI_CMD_PAUSE_IE_MASK;
	//if  ( chip_config->com_mod == DMA_TRANSFER )
	reg_val |= ( chip_config->pause << SPI_CMD_PAUSE_EN_OFFSET );
	reg_val |= ( chip_config->pause << SPI_CMD_PAUSE_IE_OFFSET );
	SPI_WRITE( SPI_REG_CMD, reg_val );

	/*set finish interrupt always enable*/
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~ SPI_CMD_FINISH_IE_MASK;
//	reg_val |= ( chip_config->finish_intr << SPI_CMD_FINISH_IE_OFFSET );
	reg_val |= ( 1 << SPI_CMD_FINISH_IE_OFFSET );

	SPI_WRITE ( SPI_REG_CMD, reg_val );
	
	/*set the communication of mode*/
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~ SPI_CMD_TX_DMA_MASK;
	reg_val &= ~ SPI_CMD_RX_DMA_MASK;
	SPI_WRITE ( SPI_REG_CMD, reg_val );

	/*set deassert mode*/
	reg_val = SPI_READ ( SPI_REG_CMD );
	reg_val &= ~SPI_CMD_DEASSERT_MASK;
	reg_val |= ( chip_config->deassert << SPI_CMD_DEASSERT_OFFSET );
	SPI_WRITE ( SPI_REG_CMD, reg_val );
	}
	return 0;
}

void SpiChipConfigSet(void)
{
	dump_chip_config_default();
	SpiSetup(chip_config);
}

/*
* for test
*/
static struct mt_chip_conf mt_chip_conf_test = {
	.setuptime = 3,
	.holdtime = 3,
    .high_time = 50,
	.low_time = 50,
	.cs_idletime = 2,
	.ulthgh_thrsh = 0,
	.cpol = 0,
    .cpha = 0,
	.rx_mlsb = 1, 
    .tx_mlsb = 1,
    .tx_endian = 0,
	.rx_endian = 0,
    .com_mod = FIFO_TRANSFER,
    .pause = PAUSE_MODE_ENABLE,
    .finish_intr = 1,
    .deassert = 0,
	.ulthigh = 0,
    .tckdly = 0
};

int32_t SpiNextXfer(struct spi_transfer *xfer)
{
	//struct spi_transfer	*xfer;
  	uint32_t  mode,cnt,i;
	int ret = 0;	
	char xfer_rec[16];
	struct mt_chip_conf *chip_config = GetChipConfig();
	
	if(chip_config == NULL) {
	  SPI_ERR("SpiNextXfer get chip_config is NULL\n");	
	  return E_INVALID;
	}
	
	//dump_chip_config(chip_config);
	if((!IsInterruptEnable())){
		SPI_MSG("interrupt is disable\n");
		ret = E_LIMIT;
		goto fail;
	}
	
   #ifndef SPI_AUTO_SELECT_MODE
	if(xfer->is_dma_used == 1)
		chip_config->com_mod = DMA_TRANSFER;
	else
		chip_config->com_mod = FIFO_TRANSFER;
   #endif
   
	mode = chip_config->com_mod;
		
	
//	SPI_MSG("start xfer 0x%p, mode %d, len %u\n",xfer,mode,xfer->len);

	SPI_MSG("xfer %p: len %04u tx %p/%08x rx %p/%08x, mode %d\n",
			xfer, xfer->len,
			xfer->tx_buf, xfer->tx_dma,
			xfer->rx_buf, xfer->rx_dma, mode);


	if((mode == FIFO_TRANSFER)){
		if(xfer->len > SPI_FIFO_SIZE ){ 
			ret = E_INVALID;
			SPI_MSG("xfer len is invalid over fifo size\n");
			goto fail;
		}
	}
	SPI_MSG("is_last_xfer=%d,xfer->is_transfer_end =%d\n",is_last_xfer,xfer->is_transfer_end )

	/*
       * cannot 1K align & FIFO->DMA need used pause mode
       * this is to clear pause bit (CS turn to idle after data transfer done)
	*/
    if(mode == DMA_TRANSFER) {
	    if((is_last_xfer == 1) && (xfer->is_transfer_end == 1))
		    clear_pause_bit();
    }else if(mode == FIFO_TRANSFER) {
		if(xfer->is_transfer_end == 1)
		    clear_pause_bit();
    }else {
        SPI_MSG("xfer mode is invalid !\n");
			goto fail;
    }

  	//SetPauseStatus(IDLE); //runing status, nothing about pause mode
	//disable DMA
	SpiDisableDma();
	//spi_clear_fifo(ms, FIFO_ALL);

	/*need setting transfer data length & loop count*/
	ret = SpiSetupPacket(xfer);
	//dump_reg_config();
	if(ret < 0){
		goto fail;
	}
	
	/*Using FIFO to send data*/	
	if((mode == FIFO_TRANSFER)){
		cnt = (xfer->len%4)?(xfer->len/4 + 1):(xfer->len/4);
		for(i = 0; i < cnt; i++){
			SPI_WRITE(SPI_REG_TX_DATA,*( ( uint32_t * ) xfer->tx_buf + i ) );
			SPI_MSG("tx_buf data is:%x\n",*( ( uint32_t * ) xfer->tx_buf + i ) );
			SPI_MSG("tx_buf addr is:%p\n", (uint32_t *)xfer->tx_buf + i);			
		}
	}
	/*Using DMA to send data*/	
	if((mode == DMA_TRANSFER)){
		SpiEnableDma(xfer, mode);
		cnt = (xfer->len%4)?(xfer->len/4 + 1):(xfer->len/4);
		for(i = 0; i < cnt; i++){
			//SPI_MSG("tx_buf addr is:%p\n", (uint32_t *)xfer->tx_buf + i);		
			//SPI_MSG("tx_buf data is:%x\n",*( ( uint32_t * ) xfer->tx_buf + i ) );
			//SPI_MSG("tx_dma addr is:%x\n", SPI_TX_DMA_VA_BASE+get_pa_offset(xfer->tx_dma)+4*i);			
			SPI_WRITE((SPI_TX_DMA_VA_BASE+4*i) , *( ( uint32_t * ) xfer->tx_buf + i)) ;
		}
	}
    dump_reg_config();
	SPI_MSG("running=%d.\n",running);
#if 1
	if(GetPauseStatus() == PAUSED){ //running
		SPI_MSG("pause status to resume.\n");
		SetPauseStatus(INPROGRESS);	//set running status		
		SpiResumeTransfer();
	}else if(GetPauseStatus() == IDLE){
		SPI_MSG("The xfer start\n");
		/*if there is only one transfer, pause bit should not be set.*/
		if((chip_config->pause)){ // need to think whether is last  msg <&& !is_last_xfer(msg,xfer)>
			SPI_MSG("set pause mode.\n");
			SetPauseBit();
		}
		/*All register must be prepared before setting the start bit [SMP]*/

		SetPauseStatus(INPROGRESS);	//set running status		
		SpiStartTransfer();
		
	}else{
		SPI_MSG("Wrong status\n");
		ret = -1;
		goto fail;
	}
#endif
	SPI_MSG("xfer,%3d\n",xfer->len);
	//SpiStartTransfer();

	/*exit pause mode*/
	if(GetPauseStatus() == PAUSED && is_last_xfer==1){		
		ClearResumeBit();
	}

	return 0;
fail:
	
	TransferDmaUnmapping();
	SetPauseStatus(IDLE);	//set running status
	ResetSpi(); 
	SetIrqFlag(IRQ_IDLE);
	Set_timeout_flag();

	if(chip_config->com_mod == DMA_TRANSFER) {
		SPI_MSG(" IPC fail SpiDMAUnMapping \n");
		SpiDMAUnMapping();
	}
	SPI_MSG("spi IPC fail 1111\n");
	
	const threadid_t threadId = drApiGetLocalThreadid(DRIVER_THREAD_NO_IPCH);
	drApiResult_t drRes = drApiIpcSignal(threadId);
	if (DRAPI_OK != drRes) {
		SPI_MSG("spi wakeup Signal fail, %x\n", drRes);
	}
	SPI_MSG("spi IPC fail 2222\n");
	return ret;
}

static void SpiNextMessage(struct spi_transfer *xfer)
{
	struct mt_chip_conf *chip_config;

	chip_config = GetChipConfig();
	if(chip_config == NULL) {
	  SPI_ERR("SpiNextMessage get chip_config is NULL\n");	
	  return;
	}
	//SPI_DBG("start transfer message:0x%p\n", msg);

	//ResetSpi();
	SpiSetup(chip_config);	
	SpiNextXfer(xfer);	
}

int32_t SpiTransfer(struct spi_transfer *xfer)
{
	struct mt_chip_conf *chip_config;
	unsigned long		flags;
	char msg_addr[16];	

	
	//SPI_DBG("enter,start add msg:0x%p\n",msg);
	    /*wait intrrupt had been clear*/
    while(IRQ_BUSY == GetIrqFlag()) {
		/*need a pause instruction to avoid unknow exception*/
		SPI_MSG("wait IRQ handle finish\n");
    }

	/*set flag to block next transfer*/
    SetIrqFlag(IRQ_BUSY);

    
  #ifdef SPI_PIN_SWITCH_MODE

     if(xfer->cs_change ) { //select SPIB--ese
 
	   ////spi a set gpio mode and out and high
       SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_CS_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DIR_SET,SPI_GPIO_A_CS_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DOUT_SET,SPI_GPIO_A_CS_DOUT_SHIFT); /*High*/
	   
	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_MI_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DIR_SET,SPI_GPIO_A_MI_DIR_SHIFT);// INPUT AND PULL UP
	   SPI_WRITE(SPI_GPIO_A_DOUT_SET,SPI_GPIO_A_MI_DOUT_SHIFT); /*High*/

	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_MO_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DIR_SET,SPI_GPIO_A_MO_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DOUT_SET,SPI_GPIO_A_MO_DOUT_SHIFT); /*High*/

	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_CK_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DIR_SET,SPI_GPIO_A_CK_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_A_DOUT_SET,SPI_GPIO_A_CK_DOUT_SHIFT); /*High*/
	   
	   //gpio b set spi mode
	   SPI_WRITE(SPI_REG_PAD_SEL, 0x03); //5-8pin  < --> REG pad macro = 0x03
	   
	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_CS_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_B_MODE_SET,SPI_GPIO_B_SET_CS_MODE_SHIFT); 

	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_MI_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_B_MODE_SET,SPI_GPIO_B_SET_MI_MODE_SHIFT); 
	   
	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_MO_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_B_MODE_SET,SPI_GPIO_B_SET_MO_MODE_SHIFT); 

	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_CK_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_B_MODE_SET,SPI_GPIO_B_SET_CK_MODE_SHIFT);

		#if 1
	    	SPI_MSG("cs_change = 1: SPIAMODE=0x%x\n",SPI_READ(SPI_GPIO_A_MODE));
			SPI_MSG("cs_change = 1: SPIADIR=0x%x\n",SPI_READ(SPI_GPIO_A_DIR));
			SPI_MSG("cs_change = 1: SPIBMODE=0x%x\n",SPI_READ(SPI_GPIO_B_MODE));
			SPI_MSG("cs_change = 1: SPIBDIR=0x%x\n",SPI_READ(SPI_GPIO_B_DIR));
		#endif


    }else { // select select SPIA-focal 

	   //spi b set gpio mode  
       SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_CS_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DIR_SET,SPI_GPIO_B_CS_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DOUT_SET,SPI_GPIO_B_CS_DOUT_SHIFT); 
	   
	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_MI_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DIR_SET,SPI_GPIO_B_MI_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DOUT_SET,SPI_GPIO_B_MI_DOUT_SHIFT); 
	   
	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_MO_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DIR_SET,SPI_GPIO_B_MO_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DOUT_SET,SPI_GPIO_B_MO_DOUT_SHIFT); 

	   SPI_WRITE(SPI_GPIO_B_MODE_CLR,SPI_GPIO_B_CLR_CK_MODE_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DIR_SET,SPI_GPIO_B_CK_DIR_SHIFT);
	   SPI_WRITE(SPI_GPIO_B_DOUT_SET,SPI_GPIO_B_CK_DOUT_SHIFT); 

	   
	   //set spi mode( default spi )
	   SPI_WRITE(SPI_REG_PAD_SEL, 0x0); //166 -169pin  < --> REG pad macro = 0x00
	   
	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_CS_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_A_MODE_SET,SPI_GPIO_A_SET_CS_MODE_SHIFT); 

	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_MI_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_A_MODE_SET,SPI_GPIO_A_SET_MI_MODE_SHIFT); 
	   
	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_MO_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_A_MODE_SET,SPI_GPIO_A_SET_MO_MODE_SHIFT); 

	   SPI_WRITE(SPI_GPIO_A_MODE_CLR,SPI_GPIO_A_CLR_CK_MODE_SHIFT); 
	   SPI_WRITE(SPI_GPIO_A_MODE_SET,SPI_GPIO_A_SET_CK_MODE_SHIFT); 

		#if 1
	   	SPI_MSG("---SPIAMODE=0x%x---\n",SPI_READ(SPI_GPIO_A_MODE));
	    SPI_MSG("---SPIADIR=0x%x----\n",SPI_READ(SPI_GPIO_A_DIR));
		SPI_MSG("---SPIBMODE=0x%x---\n",SPI_READ(SPI_GPIO_B_MODE));
		SPI_MSG("---SPIBDIR=0x%x---\n",SPI_READ(SPI_GPIO_B_DIR));
		#endif
    }
		
  #endif
   
	//dump_reg_config();
	
	/*
       * Through xfer->len to select DMA Or FIFO mode
	*/
   #ifdef SPI_AUTO_SELECT_MODE
	if(xfer) {
		/*if transfer len > 16, used DMA mode*/
        if(xfer->len > SPI_DATA_SIZE) {
			chip_config = GetChipConfig();
			chip_config->com_mod = DMA_TRANSFER;
			SPI_MSG("data size > 16, select DMA mode !\n");
        }
	}else {
        SPI_ERR("xfer is NULL pointer. \n" );
	}
   #endif
   
	if((!xfer)){
		SPI_ERR("msg is NULL pointer. \n" );
		goto out;
	}
	if((NULL == xfer)){		
		SPI_ERR("the message is NULL.\n" );
		goto out;
	}

	//list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!((xfer->tx_buf || xfer->rx_buf) && xfer->len)) {
			SPI_ERR("missing tx %p or rx %p buf, len%d\n",xfer->tx_buf,xfer->rx_buf,xfer->len);
			//msg->status = -EINVAL;
			goto out;
		}

		/*
		 * DMA map early, for performance (empties dcache ASAP) and
		 * better fault reporting. 
		 *
		 * NOTE that if dma_unmap_single() ever starts to do work on
		 * platforms supported by this driver, we would need to clean
		 * up mappings for previously-mapped transfers.
		 */
		if ((!xfer->is_dma_used)) {
			if (TransferDmaMapping() < 0)
				return E_DRAPI_CANNOT_MAP;
		}

	if (xfer){
		SpiNextMessage(xfer);
	}

	return 0;

out:
	return -1;

}

/*
*  Registers Mapping
*/
static bool gRegHasMapped = false;
int32_t SpiMapRegister(void)
{
    int32_t  status = DRAPI_OK;
//    uint32_t mapFlag = MAP_READABLE | MAP_WRITABLE | MAP_ALLOW_NONSECURE| MAP_IO;
    uint32_t mapFlag = MAP_READABLE | MAP_WRITABLE | MAP_IO;

    // register map
    do
    {
        if(gRegHasMapped)
        {
            break;
        }

		drApiMapPhysicalBuffer((uint64_t)SPI_REG_PA_BASE,0x1000,mapFlag,(void **)&SpiBaseVA);
		SPI_MSG("spi_map_regiser, hasMapped [%x]\n", (SpiBaseVA));
	   	drApiMapPhysicalBuffer((uint64_t)SPI_CFG_PA_BASE,0x1000,mapFlag,(void **)&SpiCfgBaseVA);
		SPI_MSG("spi_map_regiser, hasMapped [%x]\n", (SpiCfgBaseVA));
	   	drApiMapPhysicalBuffer((uint64_t)SPI_DAPC_PA_BASE,0x1000,mapFlag,(void **)&SpiDapcBaseVA);
		SPI_MSG("spi_map_regiser, hasMapped [%x]\n", (SpiDapcBaseVA));
		
	  #ifdef SPI_PIN_SWITCH_MODE
        drApiMapPhysicalBuffer((uint64_t)SPI_GPIO_PA_BASE,0x1000,mapFlag,(void **)&SpiGpioBaseVA);
		SPI_MSG("spi_map cs gpio, hasMapped [%x]\n", (SpiGpioBaseVA));
	  #endif
        gRegHasMapped = true;
        SPI_MSG("spi_map_regiser, hasMapped [%d]\n", gRegHasMapped);
    }while (0);

    SPI_MSG("spi_map_regiser done[%d]\n", status);
    return status;
}

/*
* SPI TEST
*/
void SpiFifoTest(void)
{
	 SPI_MSG("Peng test 0\n");
	 uint32_t i = 0;
     uint32_t tx_data = 0x12345678;
	 uint32_t rx_data = 0x0;
	 struct spi_transfer xfer;
	 struct spi_transfer *spiData = &xfer;	   
	 //(uint32_t*)spiData->tx_buf = &tx_data; 
	 xfer.tx_buf = &tx_data;
	 xfer.rx_buf = &rx_data;
	 xfer.len = 8;
	 //dump_reg_config();
     SPI_MSG("SPI 1: tx_buf(0x%x)\n", (uint32_t*)(&xfer.tx_buf));
   //  spiData.len = SPI_DATA_LENGTH;
     SPI_MSG("Peng test 1\n");
     SpiChipConfig(&mt_chip_conf_test);
	 //dump_chip_config(chip_config);
	 SpiSetupTransfer(&xfer);
	 for(i=0;i<10;i++) {
        SpiTransfer(&xfer);
	 }
	/*
         SpiChipConfig(NULL);
         SPI_MSG("SpiChipConfig\n");
	  SpiSetup(chip_config);
	  SPI_MSG("SpiSetup\n");
	  SpiSetupTransfer(&xfer);
	  SPI_MSG("SpiSetupTransfer\n");
	//for(i=0;i<10;i++) {
    	       SpiNextXfer(&xfer);
		SPI_MSG("SpiNextXfer\n");
	//}
	*/
	//dump_reg_config();
	SPI_MSG("SPI DL: tx_buf(0x%x)\n", *(uint32_t*)(xfer.tx_buf));
    SPI_MSG("SPI DL: rx_buf(0x%x)\n", *(uint32_t*)(xfer.rx_buf));
}

int32_t SpiInit(void)
{
   // EngineStruct          *pEngine;
    //TaskStruct            *pTask;
   // int32_t               index;
   SPI_MSG("-->init: core\n");

    // secure: register map
    if(DRAPI_OK != SpiMapRegister())
    {
        return E_DRAPI_CANNOT_MAP;
    }

    // Reset overall context
    //memset(&gSpiContext, 0x0, sizeof(ContextStruct));
    //SPI_MSG("SPI FIFO TEST\n");
    //SpiFifoTest();
    SetIrqFlag(IRQ_IDLE);
    SPI_MSG("-->init: core done[%d]\n");
    return 0;
}

