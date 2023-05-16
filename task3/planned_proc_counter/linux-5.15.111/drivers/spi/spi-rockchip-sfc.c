// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Serial Flash Controller Driver
 *
 * Copyright (c) 2017-2021, Rockchip Inc.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *	   Chris Morgan <macroalpha82@gmail.com>
 *	   Jon Lin <Jon.lin@rock-chips.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/spi/spi-mem.h>

/* System control */
#define SFC_CTRL			0x0
#define  SFC_CTRL_PHASE_SEL_NEGETIVE	BIT(1)
#define  SFC_CTRL_CMD_BITS_SHIFT	8
#define  SFC_CTRL_ADDR_BITS_SHIFT	10
#define  SFC_CTRL_DATA_BITS_SHIFT	12

/* Interrupt mask */
#define SFC_IMR				0x4
#define  SFC_IMR_RX_FULL		BIT(0)
#define  SFC_IMR_RX_UFLOW		BIT(1)
#define  SFC_IMR_TX_OFLOW		BIT(2)
#define  SFC_IMR_TX_EMPTY		BIT(3)
#define  SFC_IMR_TRAN_FINISH		BIT(4)
#define  SFC_IMR_BUS_ERR		BIT(5)
#define  SFC_IMR_NSPI_ERR		BIT(6)
#define  SFC_IMR_DMA			BIT(7)

/* Interrupt clear */
#define SFC_ICLR			0x8
#define  SFC_ICLR_RX_FULL		BIT(0)
#define  SFC_ICLR_RX_UFLOW		BIT(1)
#define  SFC_ICLR_TX_OFLOW		BIT(2)
#define  SFC_ICLR_TX_EMPTY		BIT(3)
#define  SFC_ICLR_TRAN_FINISH		BIT(4)
#define  SFC_ICLR_BUS_ERR		BIT(5)
#define  SFC_ICLR_NSPI_ERR		BIT(6)
#define  SFC_ICLR_DMA			BIT(7)

/* FIFO threshold level */
#define SFC_FTLR			0xc
#define  SFC_FTLR_TX_SHIFT		0
#define  SFC_FTLR_TX_MASK		0x1f
#define  SFC_FTLR_RX_SHIFT		8
#define  SFC_FTLR_RX_MASK		0x1f

/* Reset FSM and FIFO */
#define SFC_RCVR			0x10
#define  SFC_RCVR_RESET			BIT(0)

/* Enhanced mode */
#define SFC_AX				0x14

/* Address Bit number */
#define SFC_ABIT			0x18

/* Interrupt status */
#define SFC_ISR				0x1c
#define  SFC_ISR_RX_FULL_SHIFT		BIT(0)
#define  SFC_ISR_RX_UFLOW_SHIFT		BIT(1)
#define  SFC_ISR_TX_OFLOW_SHIFT		BIT(2)
#define  SFC_ISR_TX_EMPTY_SHIFT		BIT(3)
#define  SFC_ISR_TX_FINISH_SHIFT	BIT(4)
#define  SFC_ISR_BUS_ERR_SHIFT		BIT(5)
#define  SFC_ISR_NSPI_ERR_SHIFT		BIT(6)
#define  SFC_ISR_DMA_SHIFT		BIT(7)

/* FIFO status */
#define SFC_FSR				0x20
#define  SFC_FSR_TX_IS_FULL		BIT(0)
#define  SFC_FSR_TX_IS_EMPTY		BIT(1)
#define  SFC_FSR_RX_IS_EMPTY		BIT(2)
#define  SFC_FSR_RX_IS_FULL		BIT(3)
#define  SFC_FSR_TXLV_MASK		GENMASK(12, 8)
#define  SFC_FSR_TXLV_SHIFT		8
#define  SFC_FSR_RXLV_MASK		GENMASK(20, 16)
#define  SFC_FSR_RXLV_SHIFT		16

/* FSM status */
#define SFC_SR				0x24
#define  SFC_SR_IS_IDLE			0x0
#define  SFC_SR_IS_BUSY			0x1

/* Raw interrupt status */
#define SFC_RISR			0x28
#define  SFC_RISR_RX_FULL		BIT(0)
#define  SFC_RISR_RX_UNDERFLOW		BIT(1)
#define  SFC_RISR_TX_OVERFLOW		BIT(2)
#define  SFC_RISR_TX_EMPTY		BIT(3)
#define  SFC_RISR_TRAN_FINISH		BIT(4)
#define  SFC_RISR_BUS_ERR		BIT(5)
#define  SFC_RISR_NSPI_ERR		BIT(6)
#define  SFC_RISR_DMA			BIT(7)

/* Version */
#define SFC_VER				0x2C
#define  SFC_VER_3			0x3
#define  SFC_VER_4			0x4
#define  SFC_VER_5			0x5

/* Delay line controller resiter */
#define SFC_DLL_CTRL0			0x3C
#define SFC_DLL_CTRL0_SCLK_SMP_DLL	BIT(15)
#define SFC_DLL_CTRL0_DLL_MAX_VER4	0xFFU
#define SFC_DLL_CTRL0_DLL_MAX_VER5	0x1FFU

/* Master trigger */
#define SFC_DMA_TRIGGER			0x80
#define SFC_DMA_TRIGGER_START		1

/* Src or Dst addr for master */
#define SFC_DMA_ADDR			0x84

/* Length control register extension 32GB */
#define SFC_LEN_CTRL			0x88
#define SFC_LEN_CTRL_TRB_SEL		1
#define SFC_LEN_EXT			0x8C

/* Command */
#define SFC_CMD				0x100
#define  SFC_CMD_IDX_SHIFT		0
#define  SFC_CMD_DUMMY_SHIFT		8
#define  SFC_CMD_DIR_SHIFT		12
#define  SFC_CMD_DIR_RD			0
#define  SFC_CMD_DIR_WR			1
#define  SFC_CMD_ADDR_SHIFT		14
#define  SFC_CMD_ADDR_0BITS		0
#define  SFC_CMD_ADDR_24BITS		1
#define  SFC_CMD_ADDR_32BITS		2
#define  SFC_CMD_ADDR_XBITS		3
#define  SFC_CMD_TRAN_BYTES_SHIFT	16
#define  SFC_CMD_CS_SHIFT		30

/* Address */
#define SFC_ADDR			0x104

/* Data */
#define SFC_DATA			0x108

/* The controller and documentation reports that it supports up to 4 CS
 * devices (0-3), however I have only been able to test a single CS (CS 0)
 * due to the configuration of my device.
 */
#define SFC_MAX_CHIPSELECT_NUM		4

/* The SFC can transfer max 16KB - 1 at one time
 * we set it to 15.5KB here for alignment.
 */
#define SFC_MAX_IOSIZE_VER3		(512 * 31)

/* DMA is only enabled for large data transmission */
#define SFC_DMA_TRANS_THRETHOLD		(0x40)

/* Maximum clock values from datasheet suggest keeping clock value under
 * 150MHz. No minimum or average value is suggested.
 */
#define SFC_MAX_SPEED		(150 * 1000 * 1000)

struct rockchip_sfc {
	struct device *dev;
	void __iomem *regbase;
	struct clk *hclk;
	struct clk *clk;
	u32 frequency;
	/* virtual mapped addr for dma_buffer */
	void *buffer;
	dma_addr_t dma_buffer;
	struct completion cp;
	bool use_dma;
	u32 max_iosize;
	u16 version;
};

static int rockchip_sfc_reset(struct rockchip_sfc *sfc)
{
	int err;
	u32 status;

	writel_relaxed(SFC_RCVR_RESET, sfc->regbase + SFC_RCVR);

	err = readl_poll_timeout(sfc->regbase + SFC_RCVR, status,
				 !(status & SFC_RCVR_RESET), 20,
				 jiffies_to_usecs(HZ));
	if (err)
		dev_err(sfc->dev, "SFC reset never finished\n");

	/* Still need to clear the masked interrupt from RISR */
	writel_relaxed(0xFFFFFFFF, sfc->regbase + SFC_ICLR);

	dev_dbg(sfc->dev, "reset\n");

	return err;
}

static u16 rockchip_sfc_get_version(struct rockchip_sfc *sfc)
{
	return  (u16)(readl(sfc->regbase + SFC_VER) & 0xffff);
}

static u32 rockchip_sfc_get_max_iosize(struct rockchip_sfc *sfc)
{
	return SFC_MAX_IOSIZE_VER3;
}

static void rockchip_sfc_irq_unmask(struct rockchip_sfc *sfc, u32 mask)
{
	u32 reg;

	/* Enable transfer complete interrupt */
	reg = readl(sfc->regbase + SFC_IMR);
	reg &= ~mask;
	writel(reg, sfc->regbase + SFC_IMR);
}

static void rockchip_sfc_irq_mask(struct rockchip_sfc *sfc, u32 mask)
{
	u32 reg;

	/* Disable transfer finish interrupt */
	reg = readl(sfc->regbase + SFC_IMR);
	reg |= mask;
	writel(reg, sfc->regbase + SFC_IMR);
}

static int rockchip_sfc_init(struct rockchip_sfc *sfc)
{
	writel(0, sfc->regbase + SFC_CTRL);
	writel(0xFFFFFFFF, sfc->regbase + SFC_ICLR);
	rockchip_sfc_irq_mask(sfc, 0xFFFFFFFF);
	if (rockchip_sfc_get_version(sfc) >= SFC_VER_4)
		writel(SFC_LEN_CTRL_TRB_SEL, sfc->regbase + SFC_LEN_CTRL);

	return 0;
}

static int rockchip_sfc_wait_txfifo_ready(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	ret = readl_poll_timeout(sfc->regbase + SFC_FSR, status,
				 status & SFC_FSR_TXLV_MASK, 0,
				 timeout_us);
	if (ret) {
		dev_dbg(sfc->dev, "sfc wait tx fifo timeout\n");

		return -ETIMEDOUT;
	}

	return (status & SFC_FSR_TXLV_MASK) >> SFC_FSR_TXLV_SHIFT;
}

static int rockchip_sfc_wait_rxfifo_ready(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	ret = readl_poll_timeout(sfc->regbase + SFC_FSR, status,
				 status & SFC_FSR_RXLV_MASK, 0,
				 timeout_us);
	if (ret) {
		dev_dbg(sfc->dev, "sfc wait rx fifo timeout\n");

		return -ETIMEDOUT;
	}

	return (status & SFC_FSR_RXLV_MASK) >> SFC_FSR_RXLV_SHIFT;
}

static void rockchip_sfc_adjust_op_work(struct spi_mem_op *op)
{
	if (unlikely(op->dummy.nbytes && !op->addr.nbytes)) {
		/*
		 * SFC not support output DUMMY cycles right after CMD cycles, so
		 * treat it as ADDR cycles.
		 */
		op->addr.nbytes = op->dummy.nbytes;
		op->addr.buswidth = op->dummy.buswidth;
		op->addr.val = 0xFFFFFFFFF;

		op->dummy.nbytes = 0;
	}
}

static int rockchip_sfc_xfer_setup(struct rockchip_sfc *sfc,
				   struct spi_mem *mem,
				   const struct spi_mem_op *op,
				   u32 len)
{
	u32 ctrl = 0, cmd = 0;

	/* set CMD */
	cmd = op->cmd.opcode;
	ctrl |= ((op->cmd.buswidth >> 1) << SFC_CTRL_CMD_BITS_SHIFT);

	/* set ADDR */
	if (op->addr.nbytes) {
		if (op->addr.nbytes == 4) {
			cmd |= SFC_CMD_ADDR_32BITS << SFC_CMD_ADDR_SHIFT;
		} else if (op->addr.nbytes == 3) {
			cmd |= SFC_CMD_ADDR_24BITS << SFC_CMD_ADDR_SHIFT;
		} else {
			cmd |= SFC_CMD_ADDR_XBITS << SFC_CMD_ADDR_SHIFT;
			writel(op->addr.nbytes * 8 - 1, sfc->regbase + SFC_ABIT);
		}

		ctrl |= ((op->addr.buswidth >> 1) << SFC_CTRL_ADDR_BITS_SHIFT);
	}

	/* set DUMMY */
	if (op->dummy.nbytes) {
		if (op->dummy.buswidth == 4)
			cmd |= op->dummy.nbytes * 2 << SFC_CMD_DUMMY_SHIFT;
		else if (op->dummy.buswidth == 2)
			cmd |= op->dummy.nbytes * 4 << SFC_CMD_DUMMY_SHIFT;
		else
			cmd |= op->dummy.nbytes * 8 << SFC_CMD_DUMMY_SHIFT;
	}

	/* set DATA */
	if (sfc->version >= SFC_VER_4) /* Clear it if no data to transfer */
		writel(len, sfc->regbase + SFC_LEN_EXT);
	else
		cmd |= len << SFC_CMD_TRAN_BYTES_SHIFT;
	if (len) {
		if (op->data.dir == SPI_MEM_DATA_OUT)
			cmd |= SFC_CMD_DIR_WR << SFC_CMD_DIR_SHIFT;

		ctrl |= ((op->data.buswidth >> 1) << SFC_CTRL_DATA_BITS_SHIFT);
	}
	if (!len && op->addr.nbytes)
		cmd |= SFC_CMD_DIR_WR << SFC_CMD_DIR_SHIFT;

	/* set the Controller */
	ctrl |= SFC_CTRL_PHASE_SEL_NEGETIVE;
	cmd |= mem->spi->chip_select << SFC_CMD_CS_SHIFT;

	dev_dbg(sfc->dev, "sfc addr.nbytes=%x(x%d) dummy.nbytes=%x(x%d)\n",
		op->addr.nbytes, op->addr.buswidth,
		op->dummy.nbytes, op->dummy.buswidth);
	dev_dbg(sfc->dev, "sfc ctrl=%x cmd=%x addr=%llx len=%x\n",
		ctrl, cmd, op->addr.val, len);

	writel(ctrl, sfc->regbase + SFC_CTRL);
	writel(cmd, sfc->regbase + SFC_CMD);
	if (op->addr.nbytes)
		writel(op->addr.val, sfc->regbase + SFC_ADDR);

	return 0;
}

static int rockchip_sfc_write_fifo(struct rockchip_sfc *sfc, const u8 *buf, int len)
{
	u8 bytes = len & 0x3;
	u32 dwords;
	int tx_level;
	u32 write_words;
	u32 tmp = 0;

	dwords = len >> 2;
	while (dwords) {
		tx_level = rockchip_sfc_wait_txfifo_ready(sfc, 1000);
		if (tx_level < 0)
			return tx_level;
		write_words = min_t(u32, tx_level, dwords);
		iowrite32_rep(sfc->regbase + SFC_DATA, buf, write_words);
		buf += write_words << 2;
		dwords -= write_words;
	}

	/* write the rest non word aligned bytes */
	if (bytes) {
		tx_level = rockchip_sfc_wait_txfifo_ready(sfc, 1000);
		if (tx_level < 0)
			return tx_level;
		memcpy(&tmp, buf, bytes);
		writel(tmp, sfc->regbase + SFC_DATA);
	}

	return len;
}

static int rockchip_sfc_read_fifo(struct rockchip_sfc *sfc, u8 *buf, int len)
{
	u8 bytes = len & 0x3;
	u32 dwords;
	u8 read_words;
	int rx_level;
	int tmp;

	/* word aligned access only */
	dwords = len >> 2;
	while (dwords) {
		rx_level = rockchip_sfc_wait_rxfifo_ready(sfc, 1000);
		if (rx_level < 0)
			return rx_level;
		read_words = min_t(u32, rx_level, dwords);
		ioread32_rep(sfc->regbase + SFC_DATA, buf, read_words);
		buf += read_words << 2;
		dwords -= read_words;
	}

	/* read the rest non word aligned bytes */
	if (bytes) {
		rx_level = rockchip_sfc_wait_rxfifo_ready(sfc, 1000);
		if (rx_level < 0)
			return rx_level;
		tmp = readl(sfc->regbase + SFC_DATA);
		memcpy(buf, &tmp, bytes);
	}

	return len;
}

static int rockchip_sfc_fifo_transfer_dma(struct rockchip_sfc *sfc, dma_addr_t dma_buf, size_t len)
{
	writel(0xFFFFFFFF, sfc->regbase + SFC_ICLR);
	writel((u32)dma_buf, sfc->regbase + SFC_DMA_ADDR);
	writel(SFC_DMA_TRIGGER_START, sfc->regbase + SFC_DMA_TRIGGER);

	return len;
}

static int rockchip_sfc_xfer_data_poll(struct rockchip_sfc *sfc,
				       const struct spi_mem_op *op, u32 len)
{
	dev_dbg(sfc->dev, "sfc xfer_poll len=%x\n", len);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		return rockchip_sfc_write_fifo(sfc, op->data.buf.out, len);
	else
		return rockchip_sfc_read_fifo(sfc, op->data.buf.in, len);
}

static int rockchip_sfc_xfer_data_dma(struct rockchip_sfc *sfc,
				      const struct spi_mem_op *op, u32 len)
{
	int ret;

	dev_dbg(sfc->dev, "sfc xfer_dma len=%x\n", len);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		memcpy(sfc->buffer, op->data.buf.out, len);

	ret = rockchip_sfc_fifo_transfer_dma(sfc, sfc->dma_buffer, len);
	if (!wait_for_completion_timeout(&sfc->cp, msecs_to_jiffies(2000))) {
		dev_err(sfc->dev, "DMA wait for transfer finish timeout\n");
		ret = -ETIMEDOUT;
	}
	rockchip_sfc_irq_mask(sfc, SFC_IMR_DMA);
	if (op->data.dir == SPI_MEM_DATA_IN)
		memcpy(op->data.buf.in, sfc->buffer, len);

	return ret;
}

static int rockchip_sfc_xfer_done(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	ret = readl_poll_timeout(sfc->regbase + SFC_SR, status,
				 !(status & SFC_SR_IS_BUSY),
				 20, timeout_us);
	if (ret) {
		dev_err(sfc->dev, "wait sfc idle timeout\n");
		rockchip_sfc_reset(sfc);

		ret = -EIO;
	}

	return ret;
}

static int rockchip_sfc_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct rockchip_sfc *sfc = spi_master_get_devdata(mem->spi->master);
	u32 len = op->data.nbytes;
	int ret;

	if (unlikely(mem->spi->max_speed_hz != sfc->frequency)) {
		ret = clk_set_rate(sfc->clk, mem->spi->max_speed_hz);
		if (ret)
			return ret;
		sfc->frequency = mem->spi->max_speed_hz;
		dev_dbg(sfc->dev, "set_freq=%dHz real_freq=%ldHz\n",
			sfc->frequency, clk_get_rate(sfc->clk));
	}

	rockchip_sfc_adjust_op_work((struct spi_mem_op *)op);
	rockchip_sfc_xfer_setup(sfc, mem, op, len);
	if (len) {
		if (likely(sfc->use_dma) && len >= SFC_DMA_TRANS_THRETHOLD) {
			init_completion(&sfc->cp);
			rockchip_sfc_irq_unmask(sfc, SFC_IMR_DMA);
			ret = rockchip_sfc_xfer_data_dma(sfc, op, len);
		} else {
			ret = rockchip_sfc_xfer_data_poll(sfc, op, len);
		}

		if (ret != len) {
			dev_err(sfc->dev, "xfer data failed ret %d dir %d\n", ret, op->data.dir);

			return -EIO;
		}
	}

	return rockchip_sfc_xfer_done(sfc, 100000);
}

static int rockchip_sfc_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct rockchip_sfc *sfc = spi_master_get_devdata(mem->spi->master);

	op->data.nbytes = min(op->data.nbytes, sfc->max_iosize);

	return 0;
}

static const struct spi_controller_mem_ops rockchip_sfc_mem_ops = {
	.exec_op = rockchip_sfc_exec_mem_op,
	.adjust_op_size = rockchip_sfc_adjust_op_size,
};

static irqreturn_t rockchip_sfc_irq_handler(int irq, void *dev_id)
{
	struct rockchip_sfc *sfc = dev_id;
	u32 reg;

	reg = readl(sfc->regbase + SFC_RISR);

	/* Clear interrupt */
	writel_relaxed(reg, sfc->regbase + SFC_ICLR);

	if (reg & SFC_RISR_DMA) {
		complete(&sfc->cp);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int rockchip_sfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_master *master;
	struct resource *res;
	struct rockchip_sfc *sfc;
	int ret;

	master = devm_spi_alloc_master(&pdev->dev, sizeof(*sfc));
	if (!master)
		return -ENOMEM;

	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->mem_ops = &rockchip_sfc_mem_ops;
	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_TX_QUAD | SPI_TX_DUAL | SPI_RX_QUAD | SPI_RX_DUAL;
	master->max_speed_hz = SFC_MAX_SPEED;
	master->num_chipselect = SFC_MAX_CHIPSELECT_NUM;

	sfc = spi_master_get_devdata(master);
	sfc->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sfc->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(sfc->regbase))
		return PTR_ERR(sfc->regbase);

	sfc->clk = devm_clk_get(&pdev->dev, "clk_sfc");
	if (IS_ERR(sfc->clk)) {
		dev_err(&pdev->dev, "Failed to get sfc interface clk\n");
		return PTR_ERR(sfc->clk);
	}

	sfc->hclk = devm_clk_get(&pdev->dev, "hclk_sfc");
	if (IS_ERR(sfc->hclk)) {
		dev_err(&pdev->dev, "Failed to get sfc ahb clk\n");
		return PTR_ERR(sfc->hclk);
	}

	sfc->use_dma = !of_property_read_bool(sfc->dev->of_node,
					      "rockchip,sfc-no-dma");

	if (sfc->use_dma) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(dev, "Unable to set dma mask\n");
			return ret;
		}

		sfc->buffer = dmam_alloc_coherent(dev, SFC_MAX_IOSIZE_VER3,
						  &sfc->dma_buffer,
						  GFP_KERNEL);
		if (!sfc->buffer)
			return -ENOMEM;
	}

	ret = clk_prepare_enable(sfc->hclk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable ahb clk\n");
		goto err_hclk;
	}

	ret = clk_prepare_enable(sfc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable interface clk\n");
		goto err_clk;
	}

	/* Find the irq */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to get the irq\n");
		goto err_irq;
	}

	ret = devm_request_irq(dev, ret, rockchip_sfc_irq_handler,
			       0, pdev->name, sfc);
	if (ret) {
		dev_err(dev, "Failed to request irq\n");

		goto err_irq;
	}

	ret = rockchip_sfc_init(sfc);
	if (ret)
		goto err_irq;

	sfc->max_iosize = rockchip_sfc_get_max_iosize(sfc);
	sfc->version = rockchip_sfc_get_version(sfc);

	ret = spi_register_master(master);
	if (ret)
		goto err_irq;

	return 0;

err_irq:
	clk_disable_unprepare(sfc->clk);
err_clk:
	clk_disable_unprepare(sfc->hclk);
err_hclk:
	return ret;
}

static int rockchip_sfc_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct rockchip_sfc *sfc = platform_get_drvdata(pdev);

	spi_unregister_master(master);

	clk_disable_unprepare(sfc->clk);
	clk_disable_unprepare(sfc->hclk);

	return 0;
}

static const struct of_device_id rockchip_sfc_dt_ids[] = {
	{ .compatible = "rockchip,sfc"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_sfc_dt_ids);

static struct platform_driver rockchip_sfc_driver = {
	.driver = {
		.name	= "rockchip-sfc",
		.of_match_table = rockchip_sfc_dt_ids,
	},
	.probe	= rockchip_sfc_probe,
	.remove	= rockchip_sfc_remove,
};
module_platform_driver(rockchip_sfc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Rockchip Serial Flash Controller Driver");
MODULE_AUTHOR("Shawn Lin <shawn.lin@rock-chips.com>");
MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_AUTHOR("Jon Lin <Jon.lin@rock-chips.com>");
