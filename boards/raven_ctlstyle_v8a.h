/* $Id$ */
/* board-specific CPU attached signals */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//

// Приёмник "Воронёнок". up-conversion,  AD9834 + ADF4001

#ifndef RAVEN_CTLSTYLE_V8A_H_INCLUDED
#define RAVEN_CTLSTYLE_V8A_H_INCLUDED 1

	#if CPUSTYLE_ATMEGA && F_CPU != 10000000
		#error Set F_CPU right value in project file
	#endif
	/* модели синтезаторов - схемы частотообразования */
#if 1
	/* Версии частотных схем - с преобразованием "наверх" */
	#define FQMODEL_73050		1	// 1-st IF=73.050, 2-nd IF=0.455 MHz
	//#define FQMODEL_73050_IF0P5		1	// 1-st IF=73.050, 2-nd IF=0.5 MHz
	//#define FQMODEL_80455		1	// 1-st IF=80.455, 2-nd IF=0.455 MHz
	//#define FQMODEL_64455		1	// 1-st IF=64.455, 2-nd IF=0.455 MHz
	//#define FQMODEL_64455_IF0P5		1	// 1-st IF=64.455, 2-nd IF=0.5 MHz
	//#define FQMODEL_70455		1	// 1-st IF=70.455, 2-nd IF=0.455 MHz
	//#define FQMODEL_70200		1	// 1-st if = 70.2 MHz, 2-nd IF-200 kHz
	//#define FQMODEL_60700_IF05	1	// 60.7 -> 10.7 -> 0.5
	//#define FQMODEL_60700_IF0455	1	// 60.4 -> 10.7 -> 455 kHz
	//#define FQMODEL_60725_IF05	1	// 60.725 -> 10.725 -> 0.5
	//#define FQMODEL_60700_IF02	1	// 60.7 -> 10.7 -> 0.2
	//#define FQMODEL_FPGA		1	// FPGA + IQ over I2S
	// Выбор диапазонных фильтров
	#define BANDSELSTYLERE_UPCONV56M	1	/* Up-conversion with working band .030..56 MHz */

#else
	/* Версии частотных схем - с преобразованием "наверх" */
	//#define FQMODEL_45_IF8868	1	// SW2011
	//#define FQMODEL_45_IF0P5	1	// 1-st IF=45MHz, 2-nd IF=500 kHz
	#define FQMODEL_45_IF455	1	// 1-st IF=45MHz, 2-nd IF=455 kHz
	// Выбор диапазонных фильтров
	#define BANDSELSTYLERE_UPCONV36M	1	/* Up-conversion with working band 0.1 MHz..36 MHz */
	//#define BANDSELSTYLERE_UPCONV32M	1	/* Up-conversion with working band 0.1 MHz..32 MHz */
#endif

	#define	FONTSTYLE_ITALIC	1	//
	//#define DSTYLE_UR3LMZMOD	1
	// --- вариации прошивки, специфические для разных плат

	//#define	MODEL_DIRECT	1	/* использовать прямой синтез, а не гибридный */

	#define CTLREGMODE_V8A	1		/* "Воронёнок"	*/

	// +++ вариации прошивки, специфические для плат на ATMega
	// --- вариации прошивки, специфические для плат на ATMega

	// +++ Особые варианты расположения кнопок на клавиатуре
	//#define KEYB_RA1AGG	1	/* расположение кнопок для корпуса, сделанного RA1AGG - три группы по четыре линейки кнопок друг за другом в один ряд. */
	//#define KEYB_RA1AGO	1	/* расположение кнопок для RA1AGO (приёмник воронёнок с символьным дисплеем) */
	//#define KEYB_UA1CEI	1	/* расположение кнопок для UA1CEI */
	//#define KEYB_7BUTTONS	1	/* семикнопочный вариант в железном корпусе ЦШ-01 */
	//#define KEYB_VERTICAL	1	/* расположение кнопок для плат "Павлин" */
	#define KEYB_VERTICAL_REV	1	/* расположение кнопок для плат "Воробей" и "Колибри" */
	// --- Особые варианты расположения кнопок на клавиатуре

	// +++ Одна из этих строк определяет тип дисплея, для которого компилируется прошивка
	#define LCDMODE_WH1602	1	/* тип применяемого индикатора 16*2 */
	//#define LCDMODE_WH2002	1	/* тип применяемого индикатора 20*2, возможно вместе с LCDMODE_HARD_SPI */
	//#define LCDMODE_WH1604	1	/* тип применяемого индикатора 16*4 */
	//#define LCDMODE_WH2004	1	/* тип применяемого индикатора 20*4 */
	//#define LCDMODE_SPI	1		/* LCD WE2002 over SPI line - совместно с LCDMODE_WHxxxxx */
	//#define LCDMODE_RDX0120	1	/* Индикатор 64*32 с контроллером UC1601. */
	//#define LCDMODE_RDX0077	1	/* Индикатор 128*64 с контроллером UC1601.  */
	//#define LCDMODE_RDX0154	1	/* Индикатор 132*64 с контроллером UC1601. */
	//#define LCDMODE_UC1601S_TOPDOWN	1	/* LCDMODE_RDX0154 - перевернуть изображение */
	//#define LCDMODE_UC1601S_EXTPOWER	1	/* LCDMODE_RDX0154 - без внутреннего преобразователя */
	//#define LCDMODE_TIC218	1	/* Индикатор 133*65 с контроллером PCF8535 (определять вместе с LCDMODE_UC1601) */
	//#define LCDMODE_TIC154	1	/* Индикатор 133*65 с контроллером PCF8535 (определять вместе с LCDMODE_UC1601 и LCDMODE_TIC218) */
	//#define LCDMODE_LS020 	1	/* Индикатор 176*132 Sharp LS020B8UD06 с контроллером LR38826 */
	//#define LCDMODE_LPH88		1	/* Индикатор 176*132 LPH8836-2 с контроллером Hitachi HD66773 */
	//#define LCDMODE_LPH88_TOPDOWN	1	/* LCDMODE_LPH88 - перевернуть изображение */
	//#define LCDMODE_S1D13781	1	/* Инндикатор 480*272 с контроллером Epson S1D13781 */
	//#define LCDMODE_S1D13781_TOPDOWN	1	/* LCDMODE_S1D13781 - перевернуть изображение */
	//#define LCDMODE_ILI9225	1	/* Индикатор 220*176 SF-TC220H-9223A-N_IC_ILI9225C_2011-01-15 с контроллером ILI9225С */
	//#define LCDMODE_ILI9225_TOPDOWN	1	/* LCDMODE_ILI9225 - перевернуть изображение (для выводов слева от экрана) */
	//#define LCDMODE_ILI9320	1	/* Индикатор 248*320 с контроллером ILI9320 */
	//#define LCDMODE_ILI9320_TOPDOWN	1	/* LCDMODE_ILI9320 - перевернуть изображение (для ленточки с дисплея справа) */
	//#define LCDMODE_UC1608	1		/* Индикатор 240*128 с контроллером UC1608.- монохромный */
	//#define LCDMODE_UC1608_TOPDOWN	1	/* LCDMODE_UC1608 - перевернуть изображение (для выводов сверху) */
	//#define LCDMODE_ST7735	1	/* Индикатор 160*128 с контроллером Sitronix ST7735 */
	//#define LCDMODE_ST7735_TOPDOWN	1	/* LCDMODE_ST7735 - перевернуть изображение (для выводов справа) */
	//#define LCDMODE_ILI9341	1	/* 320*240 SF-TC240T-9370-T с контроллером ILI9341 - STM32F4DISCO */
	//#define LCDMODE_ILI9341_TOPDOWN	1	/* LCDMODE_ILI9341 - перевернуть изображение (для выводов справа) */
	//#define LCDMODE_ST7565S	1	/* Индикатор WO12864C2-TFH# 128*64 с контроллером Sitronix ST7565S */
	//#define LCDMODE_ST7565S_TOPDOWN	1	/* LCDMODE_ST7565S - перевернуть изображение (для выводов сверху) */
	//#define LCDMODE_ILI9163	1	/* Индикатор LPH9157-2 176*132 с контроллером ILITEK ILI9163 - Лента дисплея справа, а выводы слева. */
	//#define LCDMODE_ILI9163_TOPDOWN	1	/* LCDMODE_ILI9163 - перевернуть изображение (для выводов справа, лента дисплея слева) */
	// --- Одна из этих строк определяет тип дисплея, для которого компилируется прошивка

	#define ENCRES_DEFAULT ENCRES_128
	//#define ENCRES_DEFAULT ENCRES_100
	//#define ENCRES_DEFAULT ENCRES_24
	#define WITHDIRECTFREQENER	1 //(! CTLSTYLE_SW2011ALL && ! CTLSTYLE_UA3DKC)
	#define WITHENCODER	1	/* для изменения частоты имеется енкодер */

	// +++ Эти строки можно отключать, уменьшая функциональность готового изделия
	//#define WITHLFM		1	/* LFM MODE */
	//#define WITHTX		1	/* включено управление передатчиком - сиквенсор, электронный ключ. */
	#define WITHCAT		1	/* используется CAT */
	//#define WITHNMEA		1	/* используется NMEA parser */
	//#define WITHDEBUG		1	/* Отладочная печать через COM-порт. Без CAT (WITHCAT) */
	#define WITHVOX		1	/* используется VOX */
	#define WITHIFSHIFT	1	/* используется IF SHIFT */
	//#define WITHIFSHIFTOFFSET	(-250)	/* Начальное занчение IF SHIFT */
	//#define WITHPBT		1	/* используется PBT (если LO3 есть) */
	#define WITHSAMEBFO	1	/* использование общих настроек BFO для приёма и передачи */

	#define WITHBCBANDS	1		/* в таблице диапазонов присутствуют вещательные диапазоны */
	#define WITHWARCBANDS	1	/* В таблице диапазонов присутствуют HF WARC диапазоны */
	#define WITHBARS	1	/* отображение S-метра и SWR-метра */
	#define WITHSWRMTR	1	/* Измеритель КСВ */
	#define WITHSHOWSWRPWR 1	/* на дисплее одновременно отображаются SWR-meter и PWR-meter */
	//#define WITHPWRMTR	1	/* Индикатор выходной мощности или */
	//#define WITHVOLTLEVEL	1	/* отображение напряжения АКБ */
	//#define WITHSWLMODE	1	/* поддержка запоминания множества частот в swl-mode */
	//#define WITHPOTWPM		1	/* используется регулировка скорости передачи в телеграфе потенциометром */
	//#define WITHTEMPSENSOR	1	/* отображение данных с датчика температуры */
	//#define WITHVOLTSENSOR	1	/* отображение данных о напряжении питания */
	#define WITHMENU 	1	/* функциональность меню может быть отключена - если настраивать нечего */

	#define WITHVIBROPLEX	1	/* возможность эмуляции передачи виброплексом */
	//#define WITHIF4DSP	1	// "Дятел"
	//#define DEFAULT_DSP_IF	12000
	//
	//#define WITHMODEM 1	// тестирование приёма/передачи BPSK31
	//#define WITHFREEDV	1	/* поддержка режима FreeDV - http://freedv.org/ */ 
	#define WITHSLEEPTIMER	1	/* выключить индикатор и вывод звука по истечениии указанного времени */
	// --- Эти строки можно отключать, уменьшая функциональность готового изделия

	//#define LO1PHASES	1		/* Прямой синтез первого гетеродина двумя DDS с програмимруемым сдвигом фазы */
	#define DEFPREAMPSTATE 	1	/* УВЧ по умолчанию включён (1) или выключен (0) */

	/* что за память настроек и частот используется в контроллере */
	//#define NVRAM_TYPE NVRAM_TYPE_FM25XXXX	// SERIAL FRAM AUTODETECT
	#define NVRAM_TYPE NVRAM_TYPE_FM25L04	// Так же при использовании FM25040A - 5 вольт, 512 байт
	//#define NVRAM_TYPE NVRAM_TYPE_FM25L16
	//#define NVRAM_TYPE NVRAM_TYPE_FM25L64
	//#define NVRAM_TYPE NVRAM_TYPE_FM25L256	// FM25L256, FM25W256
	//#define NVRAM_TYPE NVRAM_TYPE_CPUEEPROM

	//#define NVRAM_TYPE NVRAM_TYPE_AT25040A
	//#define NVRAM_TYPE NVRAM_TYPE_AT25L16
	//#define NVRAM_TYPE NVRAM_TYPE_AT25256A

	// End of NVRAM definitions section

	//#define FTW_RESOLUTION 28	/* разрядность FTW выбранного DDS */
	#define FTW_RESOLUTION 32	/* разрядность FTW выбранного DDS */
	//#define FTW_RESOLUTION 31	/* разрядность FTW выбранного DDS - ATINY2313 */

	/* Board hardware configuration */
	//#define DDS1_TYPE DDS_TYPE_AD9951
	//#define PLL1_TYPE PLL_TYPE_SI570
	//#define PLL1_FRACTIONAL_LENGTH	28	/* Si570: lower 28 bits is a fractional part */
	//#define DDS1_TYPE DDS_TYPE_ATTINY2313
	#define DDS1_TYPE DDS_TYPE_AD9834
	#define DDS2_TYPE DDS_TYPE_AD9834
	#define PLL1_TYPE PLL_TYPE_ADF4001
	//#define PLL2_TYPE PLL_TYPE_ADF4001	/* Делитель октавного гетеродина для получения сквозного диапазона */

	#if 1//FQMODEL_80455 || FQMODEL_73050 || FQMODEL_73050_IF0P5 || FQMODEL_70455 || FQMODEL_70200
		#define DDS1_CLK_DIV	2		/* Делитель опорной частоты перед подачей в DDS1 */
		#define DDS2_CLK_DIV	2		/* Делитель опорной частоты перед подачей в DDS2 */
	#else
		#define DDS1_CLK_DIV	1		/* Делитель опорной частоты перед подачей в DDS1 */
		#define DDS2_CLK_DIV	1		/* Делитель опорной частоты перед подачей в DDS2 */
	#endif
	#define HYBRID_NVFOS 4				/* количество ГУН первого гетеродина */
	#define HYBRID_PLL1_POSITIVE 1		/* в петле ФАПЧ нет инвертирующего усилителя */
	//#define HYBRID_PLL1_NEGATIVE 1		/* в петле ФАПЧ есть инвертирующий усилитель */

	/* Назначение адресов на SPI шине */
	#define targetdds1 SPI_CSEL0 	/* DDS1 */
	#define targetdds2 SPI_CSEL1 	/* DDS2 - LO3 output */
	#define targetpll1 SPI_CSEL2 	/* ADF4001 after DDS1 - divide by r1 and scale to n1. Для двойной DDS первого гетеродина - вторая DDS */
	//#define targetpll2 SPI_CSEL3 	/* ADF4001 - fixed 2-nd LO generate or LO1 divider */ 
	#define targetext1 SPI_CSEL4 	/* external devices control */ 
	#define targetupd1 SPI_CSEL5 	/* DDS IOUPDATE signals at output of SPI chip-select register */
	#define targetctl1 SPI_CSEL6 	/* control register */
	#define targetnvram SPI_CSEL7  	/* serial nvram */
	#define targetuc1608 SPI_CSEL255	/* LCD with positive chip select signal	*/

	#define targetlcd targetext1 	/* LCD over SPI line devices control */
	#define targetdsp1 targetext1 	/* external devices control */


	/* коды фильтров второй ПЧ, выдаваемые (после инверсии) на коммутаторы. */
	#define BOARD_FILTER_6P0		(BOARD_FILTERCODE_0)	/* 6 kHz filter */
	#define BOARD_FILTER_9P0		(BOARD_FILTERCODE_1)	/* only 1-st IF filter used (6, 8 or 15 khz width) */
	#define BOARD_FILTER_15P0		(BOARD_FILTERCODE_1)	/* only 1-st IF filter used (6, 8 or 15 khz width) */
	#define BOARD_FILTER_3P1		(BOARD_FILTERCODE_2)	/* 3.1 or 2.75 kHz filter */
	#define BOARD_FILTER_0P5		(BOARD_FILTERCODE_3)	/* 0.5 or 0.3 kHz filter */
	#define BOARD_FILTER_0P3		(BOARD_FILTERCODE_3)	/* 0.5 or 0.3 kHz filter */

	//#define WITHMODESETSMART 1	/* в заваисмости от частоты меняется боковая, используется вместе с WITHFIXEDBFO */
	#if FQMODEL_64455_IF0P5 || FQMODEL_73050_IF0P5
		#if WITHIF4DSP
			#define WITHMODESETFULLNFM 1	/* используется FM */
		#else /* WITHIF4DSP */
			//#define WITHMODESETFULLNFM 1	/* используется FM */
			#define WITHMODESETFULL 1
		#endif /* WITHIF4DSP */
		//#define WITHWFM	1			/* используется WFM */
		#define IF3_FMASK (IF3_FMASK_9P0 | IF3_FMASK_6P0 | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FHAVE (IF3_FMASK_9P0 | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FMASKTX	(IF3_FMASK_3P1)
	#elif FQMODEL_64455 || FQMODEL_73050 || FQMODEL_70455 || FQMODEL_80455
		// Тракты с фильтром основной слекции на 455 кГц
		//#define WITHMODESETFULL 1
		#define WITHMODESETFULLNFM 1	/* используется FM */
		//#define WITHWFM	1			/* используется WFM */
		#define IF3_FMASK (IF3_FMASK_9P0 | IF3_FMASK_6P0  | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FHAVE (IF3_FMASK_9P0 | IF3_FMASK_6P0  | IF3_FMASK_3P1)
		#define IF3_FMASKTX	(IF3_FMASK_3P1)
	#elif FQMODEL_70200
		#define WITHMODESETFULL 1
		#define IF3_FMASK (IF3_FMASK_6P0 | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FHAVE (IF3_FMASK_6P0 | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FMASKTX	(IF3_FMASK_3P1)
	#elif FQMODEL_FPGA
			#define WITHMODESETFULLNFM 1
			//#define WITHWFM	1			/* используется WFM */
			/* все возможные в данной конфигурации фильтры */
			#define IF3_FMASK	(IF3_FMASK_0P5 | IF3_FMASK_3P1 /* | IF3_FMASK_6P0 | IF3_FMASK_8P0*/)
			/* все возможные в данной конфигурации фильтры для передачи */
			#define IF3_FMASKTX	(IF3_FMASK_3P1 /*| IF3_FMASK_6P0 */)
			/* фильтры, для которых стоит признак HAVE */
			#define IF3_FHAVE	( IF3_FMASK_0P5 | IF3_FMASK_3P1 /*| IF3_FMASK_6P0 | IF3_FMASK_8P0*/)

	#else
		//#define WITHMODESETFULL 1
		#define WITHMODESETFULLNFM 1
		//#define WITHWFM	1			/* используется WFM */
		#define IF3_FMASK (IF3_FMASK_15P0 | IF3_FMASK_3P1 | IF3_FMASK_0P5)
		#define IF3_FHAVE (IF3_FMASK_15P0 | IF3_FMASK_3P1)
		#define IF3_FMASKTX	(IF3_FMASK_3P1)
	#endif

#if CPUSTYLE_ATMEGA

	#define WITHSPLIT	1	/* управление режимами расстройки одной кнопкой */
	//#define WITHSPLITEX	1	/* Трехкнопочное управление режимами расстройки */
	#define WITHCATEXT	1	/* Расширенный набор команд CAT */
	#define WITHELKEY	1
	#define WITHKBDENCODER 1	// перестройка частоты кнопками
	#define WITHKEYBOARD 1	/* в данном устройстве есть клавиатура */
	#define KEYBOARD_USE_ADC	1	/* на одной линии установлено  четыре  клавиши. на vref - 6.8K, далее 2.2К, 4.7К и 13K. */

	// Назначения входов АЦП процессора.
	enum 
	{ 
	#if WITHBARS
		SMETERIX = 0,	// S-meter
	#endif
	#if WITHTX && WITHVOX
		VOXIX = 2, AVOXIX = 1,	// VOX
	#endif
	#if WITHTX && WITHSWRMTR
		PWRI = 4,
		FWD = 4, REF = 3,	// SWR-meter
	#endif
		KI0 = 5, KI1 = 6, KI2 = 7,	// клавиатура

		XTHERMOMRRIX = BOARD_ADCMRRIN(0),	// кеш - индекc не должен повторяться в конфигурации
		PASENSEMRRIX = BOARD_ADCMRRIN(1),	// кеш - индекc не должен повторяться в конфигурации
		REFMRRIX = BOARD_ADCMRRIN(2),
		FWDMRRIX = BOARD_ADCMRRIN(3),
		PWRMRRIX = FWDMRRIX,
		VOLTMRRIX = BOARD_ADCMRRIN(4),	// кеш - индекc не должен повторяться в конфигурации
		PASENSEMRRIX2 = BOARD_ADCMRRIN(5),		// кеш - индекc не должен повторяться в конфигурации
		PAREFERMRRIX2 = BOARD_ADCMRRIN(6),		// кеш - индекc не должен повторяться в конфигурации

		//
		ki_Unused
	};
	#define KI_COUNT 3	// количество используемых под клавиатуру входов АЦП


#endif


#endif /* RAVEN_CTLSTYLE_V8A_H_INCLUDED */
