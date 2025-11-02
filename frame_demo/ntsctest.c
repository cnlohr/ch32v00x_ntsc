// Stream a continuous stream of values to Timer 1 Output 1
// ADC on Pin PD5
// NTSC on PD2
// Debug timing output for monitor on PD3

#include "ch32fun.h"
#include <stdio.h>


#include "jollywrencher.h"

#define RANDOM_STRENGTH 1

#include "lib_rand.h"

#define DMAHW DMA1_Channel5
#define DMAHW_IRQn DMA1_Channel5_IRQn
#define DMAHW_IRQHandler DMA1_Channel5_IRQHandler
#define DMAHWGL DMA1_IT_GL5
#define DMAHWTC DMA1_IT_TC5
#define DMAHWHT DMA1_IT_HT5

#ifdef CH32V003
// Leave in flash on 003, it's faster
#define LOCATION_DECORATOR
#define TIM1_SWEVGR_UG      TIM_UG
#define TIM1_CCER_CC1E      TIM_CC1E
#define TIM1_CHCTLR1_OC1M_2 TIM_OC1M_2
#define TIM1_CHCTLR1_OC1M_1 TIM_OC1M_1
#define TIM1_BDTR_MOE       TIM_MOE
#define TIM1_DMAINTENR_UDE  TIM_UDE
#define TIM1_DMAINTENR_COMDE TIM_COMDE
#define TIM1_CTLR1_CEN       TIM_CEN
#else
// Put in RAM on the 006, it's faster
#define LOCATION_DECORATOR __attribute__((section(".srodata")))
#endif

#define MBSAMPS 1024
volatile uint8_t memory_buffer[MBSAMPS];

#define RES_W 30
#define RES_H 20
uint8_t framebuffer[RES_H][RES_W];


	// steps minus 1, at 1.2MHz, value
	// H = 63.555562561 us
	// H = 76.266675073 steps.

	// p = 2.82 steps
	// q = 32.52 steps
	// r = 5.64 steps
	// p' = 35.31 steps

const uint8_t synctable1half[] = {
	4, 0,  // q
	26, 1,   // r
	0, 0,	
	0, 0
};

const uint8_t synctable12[] = {
	4, 0,  // q
	26, 1,   // r
	4, 0,  // q
	26, 1,   // r
	0, 0,	
	0, 0	
};

const uint8_t synctable3[] = {
	26, 0, // q
	4, 1,  // r
	4, 0,  // p
	26, 1, // p'
	0, 0,	
	0, 0
};

const uint8_t synctable45[] = {
	26, 0, // p'
	4, 1,  // p
	26, 0, // p'
	4, 1,  // p
	0, 0,	
	0, 0	
};

const uint8_t synctableblank[] = {
	4, 0, // r
	19, 1, // r'
	18, 1, // r'
	19, 1, // r'
	0, 0,	
	0, 0,	
};

#define FRONT_DARK 13
const uint8_t synctableline[] = {
	4, 0,
	FRONT_DARK, 1,
	RES_W, 2, // frame data
	56-RES_W-FRONT_DARK, 1,
	0, 0,
	0, 0,	
};

const uint8_t synctable3B[] = {
	4, 0,
	26, 1,
	26, 0,
	4, 1,
	0, 0,	
	0, 0,	
};


const uint8_t synctable6B[] = {
	25, 0,
	5, 1,
	3, 0,
	27, 1,
	0, 0,	
	0, 0,	
};


#define FRONTDARK 35
#define SYNCLINE  159
#define BACKDARK (253-SYNCLINE-FRONTDARK)

const uint32_t synctable_map[] = {
// First field.
	(uintptr_t)synctable12, 3,
	(uintptr_t)synctable45, 3,
	(uintptr_t)synctable12, 3,
	(uintptr_t)synctableblank, FRONTDARK, // +3 blank lines just cause.
	(uintptr_t)synctableline, SYNCLINE,
	(uintptr_t)synctableblank, BACKDARK,
	// 263 is a wacky one...  Just pretend it's like 1 and 2.
	(uintptr_t)synctable12, 3,
	// Second field, part 3.
	(uintptr_t)synctable3B, 1, 
	(uintptr_t)synctable45, 2,
	(uintptr_t)synctable6B, 1,
	(uintptr_t)synctable12, 2, // 314, 315
	(uintptr_t)synctableblank, FRONTDARK-1,
	(uintptr_t)synctableline, SYNCLINE,
	(uintptr_t)synctableblank, BACKDARK+1,
//	(uintptr_t)synctable1half, 1,

	0, 0
};


static void DataFill( uint32_t * o, int words ) LOCATION_DECORATOR;
static void DataFill( uint32_t * o, int words )
{
	// TODO: Sane starting values.
	static const uint32_t * stable = &synctable_map[0];
	static int stablecnt = 1;
	static const uint8_t * ctable_s = &synctable12[0];
	static uint8_t * framebuffer_ptr_s = &framebuffer[0][0];
	static int ctablecnt_s = 1;
	static int ctablevalue_s = 0;

	uint8_t * framebuffer_ptr;
	const uint8_t * ctable = ctable_s;
	int ctablecnt;
	int ctablevalue;

	framebuffer_ptr = framebuffer_ptr_s;
	ctablecnt = ctablecnt_s;
	ctablevalue = ctablevalue_s;

	uint32_t * oend = o + words;
	do
	{
		if( ctablecnt-- == 0 )
		{

outer_loop:
			ctablecnt = ctable[0];
			ctablevalue = ctable[1];
			ctable += 2;

			if( ctablecnt == 0 )
			{
				if( stablecnt-- == 0 )
				{
inner_loop:
					stable+=2;
					ctable = (uint8_t*)stable[0];
					stablecnt = stable[1];

					framebuffer_ptr = framebuffer_ptr_s =  &framebuffer[0][0]; 
					framebuffer_ptr -= 1 + RES_W;


					if( ctable == 0 )
					{
						stable = synctable_map-2;
						goto inner_loop;
					}
				}
				else
				{
					ctable = (uint8_t*)stable[0];
				}
				goto outer_loop;
			}
			if( ( stablecnt & 0x7 ) == 0x07 && ctable == (synctableline+4) )
			{
				framebuffer_ptr += RES_W;
			}
		}

		if( ctablevalue == 0 )
			*o = 0;
		else if( ctablevalue == 1 )
			*o = 0x02020202;
		else
		{
			//if( ctable != synctableline + 6 )
			//	printf( "%08x %08x %08x\n", (unsigned int)stable, (unsigned int)ctable, (unsigned int)synctableline );
//			if( (stablecnt&3) == 0 )
///			*o = 0x06060606;
	//		else
		//		*o = (ctablecnt&1)?0x06020202:0x06020202;

//
			*o = ((framebuffer_ptr[ctablecnt])) ? 0x06060606 : 0x02020202;
		}
	} while( (++o) != oend );

	framebuffer_ptr_s = framebuffer_ptr;
	ctable_s = ctable;
	ctablecnt_s = ctablecnt;
	ctablevalue_s = ctablevalue;
}

void DMAHW_IRQHandler( void ) __attribute__((interrupt)) LOCATION_DECORATOR;
void DMAHW_IRQHandler( void )
{
	static int frameno;
	volatile int intfr = DMA1->INTFR;
	do
	{
		DMA1->INTFCR = DMAHWGL;

		// Gets called at the end-of-a frame.
		if( intfr & DMAHWTC )
		{
			uint32_t * mbb = (uint32_t*)( memory_buffer + MBSAMPS/2 );
			DataFill( mbb, MBSAMPS/8 );
			frameno++;
		}

		// Gets called halfway through the frame
		if( intfr & DMAHWHT )
		{
			uint32_t * mbb = (uint32_t*)( memory_buffer );
			DataFill( mbb, MBSAMPS/8 );
		}
		intfr = DMA1->INTFR;
	} while( intfr );
}

int main()
{
	SystemInit();

	// Using a DMA to feed TIM1CH1 (GPIO PD2)

	// Enable GPIOD and TIM1
	// Enable DMA

	// Target 65.337us per frame segment.
	//RCC->CTLR = (RCC->CTLR & 0xffffff03) | 0x80;


	RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_TIM1 | RCC_APB2Periph_AFIO;
	
	funPinMode( PD2, GPIO_CFGLR_OUT_2Mhz_AF_PP );
	funPinMode( PD3, GPIO_CFGLR_OUT_50Mhz_PP );

	// No remapping (But you could remap if you want)
	AFIO->PCFR1 = 0;

	// Reset TIM1 to init all regs
	RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;

	// Prescaler 
	TIM1->PSC = 0x0000;
	
	// Auto Reload - sets period
	TIM1->ATRLR = 11; // 4.8MHz (0..9)
	
	// Reload immediately
	TIM1->SWEVGR |= TIM1_SWEVGR_UG;
	
	// Enable CH1N output, positive pol
	TIM1->CCER |= TIM1_CCER_CC1E;// | TIM_CC1P;
	
	// CH1 Mode is output, PWM1 (CC1S = 00, OC1M = 110)
	TIM1->CHCTLR1 |= TIM1_CHCTLR1_OC1M_2 | TIM1_CHCTLR1_OC1M_1;

	// Set the Capture Compare Register value to 50% initially
	TIM1->CH1CVR = 0;
	
	// Enable TIM1 outputs (You only need to do this on tim1)
	TIM1->BDTR |= TIM1_BDTR_MOE;

// Syncrhonization mode.
#ifdef CH32V003
	TIM1->CTLR2 =
		TIM_MMS_0 | 
	//	TIM_MMS_1 | 
		//TIM_MMS_2 | 
		0;
#else
	TIM1->CTLR2 =
		TIM1_CTLR2_MMS_1 | 
		TIM1_CTLR2_MMS_0 | 
		//TIM1_CTLR2_MMS_2 | 
		0;
#endif

	// Enable TIM1
	TIM1->CTLR1 |= TIM1_CTLR1_CEN;

	// DMA2 can be configured to attach to T1CH1, Channel 2

	DMAHW->CNTR = sizeof(memory_buffer) / sizeof(memory_buffer[0]);
	DMAHW->MADDR = (uint32_t)memory_buffer;
	DMAHW->PADDR = (uint32_t)&TIM1->CH1CVR; // This is the output register for out buffer.
	DMAHW->CFGR =
		DMA_CFGR1_DIR |                      // MEM2PERIPHERAL
		DMA_CFGR1_PL |                       // High priority.
		0 |                                  // 8-bit memory
		DMA_CFGR1_PSIZE_0 |                  // 32-bit peripheral
		DMA_CFGR1_MINC |                     // Increase memory.
		DMA_CFGR1_CIRC |                     // Circular mode.
		DMA_CFGR1_HTIE |                     // Half-trigger
		DMA_CFGR1_TCIE |                     // Whole-trigger
		DMA_CFGR1_EN;                        // Enable

	NVIC_EnableIRQ( DMAHW_IRQn );
	DMAHW->CFGR |= DMA_CFGR1_EN;

//	TIM1->DMAINTENR = TIM_TDE | TIM_CC1DE;// | TIM_UDE; (For channel 2)
	TIM1->DMAINTENR = TIM1_DMAINTENR_UDE | TIM1_DMAINTENR_COMDE; //(For channel 5)

	int i;
	int x, y;

	uint32_t step = SysTick->CNT;
	int frame = 0;




	// ADC

	// ADCCLK = 24 MHz => RCC_ADCPRE = 0: divide by 2
	RCC->CFGR0 &= ~(0x1F<<11);
	
	// Enable GPIOD and ADC
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1;
	
	// Reset the ADC to init all regs
	RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;
	
	// Set up single conversion on chl 7
	ADC1->RSQR1 = 0;
	ADC1->RSQR2 = 0;
	ADC1->RSQR3 = 5;	// 0-9 for 8 ext inputs and two internals --> ADC0
	
	ADC1->SAMPTR2 = 7<<(3*7);	// 0:7 => 3/9/15/30/43/57/73/241 cycles
		
	// turn on ADC and set rule group to sw trig
	ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;

	ADC1->CTLR2 |= ADC_SWSTART;

	while(1)
	{

		static const unsigned char sintable[] = {
			0x80, 0x83, 0x86, 0x89, 0x8c, 0x8f, 0x92, 0x95, 0x99, 0x9c, 0x9f, 0xa2, 0xa5, 0xa8, 0xab, 0xad, 
			0xb0, 0xb3, 0xb6, 0xb9, 0xbc, 0xbe, 0xc1, 0xc4, 0xc6, 0xc9, 0xcb, 0xce, 0xd0, 0xd3, 0xd5, 0xd7, 
			0xda, 0xdc, 0xde, 0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xe9, 0xeb, 0xed, 0xee, 0xf0, 0xf1, 0xf3, 0xf4, 
			0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 
			0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfd, 0xfc, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 
			0xf5, 0xf4, 0xf3, 0xf1, 0xf0, 0xee, 0xed, 0xeb, 0xe9, 0xe8, 0xe6, 0xe4, 0xe2, 0xe0, 0xde, 0xdc, 
			0xda, 0xd7, 0xd5, 0xd3, 0xd0, 0xce, 0xcb, 0xc9, 0xc6, 0xc4, 0xc1, 0xbe, 0xbc, 0xb9, 0xb6, 0xb3, 
			0xb0, 0xad, 0xab, 0xa8, 0xa5, 0xa2, 0x9f, 0x9c, 0x99, 0x95, 0x92, 0x8f, 0x8c, 0x89, 0x86, 0x83, 
			0x80, 0x7d, 0x79, 0x76, 0x73, 0x70, 0x6d, 0x6a, 0x67, 0x64, 0x61, 0x5e, 0x5b, 0x58, 0x55, 0x52, 
			0x4f, 0x4c, 0x49, 0x47, 0x44, 0x41, 0x3e, 0x3c, 0x39, 0x36, 0x34, 0x31, 0x2f, 0x2d, 0x2a, 0x28, 
			0x26, 0x24, 0x21, 0x1f, 0x1d, 0x1b, 0x1a, 0x18, 0x16, 0x14, 0x13, 0x11, 0x10, 0x0e, 0x0d, 0x0b, 
			0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x04, 0x03, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 
			0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 
			0x0a, 0x0b, 0x0d, 0x0e, 0x10, 0x11, 0x13, 0x14, 0x16, 0x18, 0x1a, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 
			0x26, 0x28, 0x2a, 0x2d, 0x2f, 0x31, 0x34, 0x36, 0x39, 0x3c, 0x3e, 0x41, 0x44, 0x47, 0x49, 0x4c, 
			0x4f, 0x52, 0x55, 0x58, 0x5b, 0x5e, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79, 0x7d, };

		uint32_t now = SysTick->CNT;
		if( (int32_t)(now-step) > 0 )
		{
			int st = sintable[frame]*2-0xff;
			for( y = 0; y < 20; y++ )
			for( x = -15; x < 16; x++ )
			{
				int ux = (x * st)>>8;
				framebuffer[y][ux+14] = jollywrencher[y][x+14];
			}
			frame++;
			if( frame == 54 ) frame = 74;
			if( frame == 182 ) frame = 202;
			if( frame == 256 ) frame = 0;

			ADC1->CTLR2 |= ADC_SWSTART;
			//	printf( "%d\n", ADC1->RDATAR );


			step += ADC1->RDATAR * 128 + 10000;//50000;
			//printf( "%d\n", ADC1->RDATAR );
		}

		funDigitalWrite( PD3, FUN_HIGH );
		funDigitalWrite( PD3, FUN_LOW );
	}
}
