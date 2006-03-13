#include "convolution\ffthelp.h"

const TCHAR PlanningRigour::Rigour[PlanningRigour::nDegrees][PlanningRigour::nStrLen] =
{
#ifdef FFTW
	TEXT("Estimate"),
		TEXT("Measure"),
		TEXT("Patient"),
		TEXT("Exhaustive"),
		TEXT("Take 1 minute")
#else
	TEXT("Default"), nStrLen)
#endif
};

const unsigned int PlanningRigour::Flag[PlanningRigour::nDegrees] =
{
#ifdef FFTW
	FFTW_ESTIMATE|FFTW_DESTROY_INPUT,
		FFTW_MEASURE|FFTW_DESTROY_INPUT,
		FFTW_PATIENT|FFTW_DESTROY_INPUT,
		FFTW_EXHAUSTIVE|FFTW_DESTROY_INPUT,
		FFTW_PATIENT|FFTW_DESTROY_INPUT
#else
	0
#endif
};

// 2^a * 2^b * 3^c * 5^d * 11^e * 13^f (e+f=0 or 1)
const DWORD OptimalDFT::OptimalDFTSize[]  =
{
				2,      3,      4,      5,      6,      7,      8,      9,     10, 
		11,     12,     13,     14,     15,     16,     18,     20,     21,     22, 
		24,     25,     26,     27,     28,     30,     32,     33,     35,     36, 
		39,     40,     42,     44,     45,     48,     49,     50,     52,     54, 
		55,     56,     60,     63,     64,     65,     66,     70,     72,     75, 
		77,     78,     80,     81,     84,     88,     90,     91,     96,     98, 
		99,    100,    104,    105,    108,    110,    112,    117,    120,    125, 
		126,    128,    130,    132,    135,    140,    144,    147,    150,    154, 
		156,    160,    162,    165,    168,    175,    176,    180,    182,    189, 
		192,    195,    196,    198,    200,    208,    210,    216,    220,    224, 
		225,    231,    234,    240,    243,    245,    250,    252,    256,    260, 
		264,    270,    273,    275,    280,    288,    294,    297,    300,    308, 
		312,    315,    320,    324,    325,    330,    336,    343,    350,    351, 
		352,    360,    364,    375,    378,    384,    385,    390,    392,    396, 
		400,    405,    416,    420,    432,    440,    441,    448,    450,    455, 
		462,    468,    480,    486,    490,    495,    500,    504,    512,    520, 
		525,    528,    539,    540,    546,    550,    560,    567,    576,    585, 
		588,    594,    600,    616,    624,    625,    630,    637,    640,    648, 
		650,    660,    672,    675,    686,    693,    700,    702,    704,    720, 
		728,    729,    735,    750,    756,    768,    770,    780,    784,    792, 
		800,    810,    819,    825,    832,    840,    864,    875,    880,    882, 
		891,    896,    900,    910,    924,    936,    945,    960,    972,    975, 
		980,    990,   1000,   1008,   1024,   1029,   1040,   1050,   1053,   1056, 
		1078,   1080,   1092,   1100,   1120,   1125,   1134,   1152,   1155,   1170, 
		1176,   1188,   1200,   1215,   1225,   1232,   1248,   1250,   1260,   1274, 
		1280,   1296,   1300,   1320,   1323,   1344,   1350,   1365,   1372,   1375, 
		1386,   1400,   1404,   1408,   1440,   1456,   1458,   1470,   1485,   1500, 
		1512,   1536,   1540,   1560,   1568,   1575,   1584,   1600,   1617,   1620, 
		1625,   1638,   1650,   1664,   1680,   1701,   1715,   1728,   1750,   1755, 
		1760,   1764,   1782,   1792,   1800,   1820,   1848,   1872,   1875,   1890, 
		1911,   1920,   1925,   1944,   1950,   1960,   1980,   2000,   2016,   2025, 
		2048,   2058,   2079,   2080,   2100,   2106,   2112,   2156,   2160,   2184, 
		2187,   2200,   2205,   2240,   2250,   2268,   2275,   2304,   2310,   2340, 
		2352,   2376,   2400,   2401,   2430,   2450,   2457,   2464,   2475,   2496, 
		2500,   2520,   2548,   2560,   2592,   2600,   2625,   2640,   2646,   2673, 
		2688,   2695,   2700,   2730,   2744,   2750,   2772,   2800,   2808,   2816, 
		2835,   2880,   2912,   2916,   2925,   2940,   2970,   3000,   3024,   3072, 
		3080,   3087,   3120,   3125,   3136,   3150,   3159,   3168,   3185,   3200, 
		3234,   3240,   3250,   3276,   3300,   3328,   3360,   3375,   3402,   3430, 
		3456,   3465,   3500,   3510,   3520,   3528,   3564,   3584,   3600,   3640, 
		3645,   3675,   3696,   3744,   3750,   3773,   3780,   3822,   3840,   3850, 
		3888,   3900,   3920,   3960,   3969,   4000,   4032,   4050,   4095,   4096, 
		4116,   4125,   4158,   4160,   4200,   4212,   4224,   4312,   4320,   4368, 
		4374,   4375,   4400,   4410,   4455,   4459,   4480,   4500,   4536,   4550, 
		4608,   4620,   4680,   4704,   4725,   4752,   4800,   4802,   4851,   4860, 
		4875,   4900,   4914,   4928,   4950,   4992,   5000,   5040,   5096,   5103, 
		5120,   5145,   5184,   5200,   5250,   5265,   5280,   5292,   5346,   5376, 
		5390,   5400,   5460,   5488,   5500,   5544,   5600,   5616,   5625,   5632, 
		5670,   5733,   5760,   5775,   5824,   5832,   5850,   5880,   5940,   6000, 
		6048,   6075,   6125,   6144,   6160,   6174,   6237,   6240,   6250,   6272, 
		6300,   6318,   6336,   6370,   6400,   6468,   6480,   6500,   6552,   6561, 
		6600,   6615,   6656,   6720,   6750,   6804,   6825,   6860,   6875,   6912, 
		6930,   7000,   7020,   7040,   7056,   7128,   7168,   7200,   7203,   7280, 
		7290,   7350,   7371,   7392,   7425,   7488,   7500,   7546,   7560,   7644, 
		7680,   7700,   7776,   7800,   7840,   7875,   7920,   7938,   8000,   8019, 
		8064,   8085,   8100,   8125,   8190,   8192,   8232,   8250,   8316,   8320, 
		8400,   8424,   8448,   8505,   8575,   8624,   8640,   8736,   8748,   8750, 
		8775,   8800,   8820,   8910,   8918,   8960,   9000,   9072,   9100,   9216, 
		9240,   9261,   9360,   9375,   9408,   9450,   9477,   9504,   9555,   9600, 
		9604,   9625,   9702,   9720,   9750,   9800,   9828,   9856,   9900,   9984, 
		10000,  10080,  10125,  10192,  10206,  10240,  10290,  10368,  10395,  10400, 
		10500,  10530,  10560,  10584,  10692,  10752,  10780,  10800,  10920,  10935, 
		10976,  11000,  11025,  11088,  11200,  11232,  11250,  11264,  11319,  11340, 
		11375,  11466,  11520,  11550,  11648,  11664,  11700,  11760,  11880,  11907, 
		12000,  12005,  12096,  12150,  12250,  12285,  12288,  12320,  12348,  12375, 
		12474,  12480,  12500,  12544,  12600,  12636,  12672,  12740,  12800,  12936, 
		12960,  13000,  13104,  13122,  13125,  13200,  13230,  13312,  13365,  13377, 
		13440,  13475,  13500,  13608,  13650,  13720,  13750,  13824,  13860,  14000, 
		14040,  14080,  14112,  14175,  14256,  14336,  14400,  14406,  14553,  14560, 
		14580,  14625,  14700,  14742,  14784,  14850,  14976,  15000,  15092,  15120, 
		15288,  15309,  15360,  15400,  15435,  15552,  15600,  15625,  15680,  15750, 
		15795,  15840,  15876,  15925,  16000,  16038,  16128,  16170,  16200,  16250, 
		16380,  16384,  16464,  16500,  16632,  16640,  16800,  16807,  16848,  16875, 
		16896,  17010,  17150,  17199,  17248,  17280,  17325,  17472,  17496,  17500, 
		17550,  17600,  17640,  17820,  17836,  17920,  18000,  18144,  18200,  18225, 
		18375,  18432,  18480,  18522,  18711,  18720,  18750,  18816,  18865,  18900, 
		18954,  19008,  19110,  19200,  19208,  19250,  19404,  19440,  19500,  19600, 
		19656,  19683,  19712,  19800,  19845,  19968,  20000,  20160,  20250,  20384, 
		20412,  20475,  20480,  20580,  20625,  20736,  20790,  20800,  21000,  21060, 
		21120,  21168,  21384,  21504,  21560,  21600,  21609,  21840,  21870,  21875, 
		21952,  22000,  22050,  22113,  22176,  22275,  22295,  22400,  22464,  22500, 
		22528,  22638,  22680,  22750,  22932,  23040,  23100,  23296,  23328,  23400, 
		23520,  23625,  23760,  23814,  24000,  24010,  24057,  24192,  24255,  24300, 
		24375,  24500,  24570,  24576,  24640,  24696,  24750,  24948,  24960,  25000, 
		25088,  25200,  25272,  25344,  25480,  25515,  25600,  25725,  25872,  25920, 
		26000,  26208,  26244,  26250,  26325,  26400,  26411,  26460,  26624,  26730, 
		26754,  26880,  26950,  27000,  27216,  27300,  27440,  27500,  27648,  27720, 
		27783,  28000,  28080,  28125,  28160,  28224,  28350,  28431,  28512,  28665, 
		28672,  28800,  28812,  28875,  29106,  29120,  29160,  29250,  29400,  29484, 
		29568,  29700,  29952,  30000,  30184,  30240,  30375,  30576,  30618,  30625, 
		30720,  30800,  30870,  31104,  31185,  31200,  31213,  31250,  31360,  31500, 
		31590,  31680,  31752,  31850,  32000,  32076,  32256,  32340,  32400,  32500, 
		32760,  32768,  32805,  32928,  33000,  33075,  33264,  33280,  33600,  33614, 
		33696,  33750,  33792,  33957,  34020,  34125,  34300,  34375,  34398,  34496, 
		34560,  34650,  34944,  34992,  35000,  35100,  35200,  35280,  35640,  35672, 
		35721,  35840,  36000,  36015,  36288,  36400,  36450,  36750,  36855,  36864, 
		36960,  37044,  37125,  37422,  37440,  37500,  37632,  37730,  37800,  37908, 
		38016,  38220,  38400,  38416,  38500,  38808,  38880,  39000,  39200,  39312, 
		39366,  39375,  39424,  39600,  39690,  39936,  40000,  40095,  40131,  40320, 
		40425,  40500,  40625,  40768,  40824,  40950,  40960,  41160,  41250,  41472, 
		41580,  41600,  42000,  42120,  42240,  42336,  42525,  42768,  42875,  43008, 
		43120,  43200,  43218,  43659,  43680,  43740,  43750,  43875,  43904,  44000, 
		44100,  44226,  44352,  44550,  44590,  44800,  44928,  45000,  45056,  45276, 
		45360,  45500,  45864,  45927,  46080,  46200,  46305,  46592,  46656,  46800, 
		46875,  47040,  47250,  47385,  47520,  47628,  47775,  48000,  48020,  48114, 
		48125,  48384,  48510,  48600,  48750,  49000,  49140,  49152,  49280,  49392, 
		49500,  49896,  49920,  50000,  50176,  50400,  50421,  50544,  50625,  50688, 
		50960,  51030,  51200,  51450,  51597,  51744,  51840,  51975,  52000,  52416, 
		52488,  52500,  52650,  52800,  52822,  52920,  53248,  53460,  53508,  53760, 
		53900,  54000,  54432,  54600,  54675,  54880,  55000,  55125,  55296,  55440, 
		55566,  56000,  56133,  56160,  56250,  56320,  56448,  56595,  56700,  56862, 
		56875,  57024,  57330,  57344,  57600,  57624,  57750,  58212,  58240,  58320, 
		58500,  58800,  58968,  59049,  59136,  59400,  59535,  59904,  60000,  60025, 
		60368,  60480,  60750,  61152,  61236,  61250,  61425,  61440,  61600,  61740, 
		61875,  62208,  62370,  62400,  62426,  62500,  62720,  63000,  63180,  63360, 
		63504,  63700,  64000,  64152,  64512,  64680,  64800,  64827,  65000,  65520, 
		65536,  65610,  65625,  65856,  66000,  66150,  66339,  66528,  66560,  66825, 
		66885,  67200,  67228,  67375,  67392,  67500,  67584,  67914,  68040,  68250, 
		68600,  68750,  68796,  68992,  69120,  69300,  69888,  69984,  70000,  70200, 
		70400,  70560,  70875,  71280,  71344,  71442,  71680,  72000,  72030,  72171, 
		72576,  72765,  72800,  72900,  73125,  73500,  73710,  73728,  73920,  74088, 
		74250,  74844,  74880,  75000,  75264,  75460,  75600,  75816,  76032,  76440, 
		76545,  76800,  76832,  77000,  77175,  77616,  77760,  78000,  78125,  78400, 
		78624,  78732,  78750,  78848,  78975,  79200,  79233,  79380,  79625,  79872, 
		80000,  80190,  80262,  80640,  80850,  81000,  81250,  81536,  81648,  81900, 
		81920,  82320,  82500,  82944,  83160,  83200,  83349,  84000,  84035,  84240, 
		84375,  84480,  84672,  85050,  85293,  85536,  85750,  85995,  86016,  86240, 
		86400,  86436,  86625,  87318,  87360,  87480,  87500,  87750,  87808,  88000, 
		88200,  88452,  88704,  89100,  89180,  89600,  89856,  90000,  90112,  90552, 
		90720,  91000,  91125,  91728,  91854,  91875,  92160,  92400,  92610,  93184, 
		93312,  93555,  93600,  93639,  93750,  94080,  94325,  94500,  94770,  95040, 
		95256,  95550,  96000,  96040,  96228,  96250,  96768,  97020,  97200,  97500, 
		98000,  98280,  98304,  98415,  98560,  98784,  99000,  99225,  99792,  99840, 
		100000, 100352, 100800, 100842, 101088, 101250, 101376, 101871, 101920, 102060, 
		102375, 102400, 102900, 103125, 103194, 103488, 103680, 103950, 104000, 104832, 
		104976, 105000, 105300, 105600, 105644, 105840, 106496, 106920, 107016, 107163, 
		107520, 107800, 108000, 108045, 108864, 109200, 109350, 109375, 109760, 110000, 
		110250, 110565, 110592, 110880, 111132, 111375, 111475, 112000, 112266, 112320, 
		112500, 112640, 112896, 113190, 113400, 113724, 113750, 114048, 114660, 114688, 
		115200, 115248, 115500, 116424, 116480, 116640, 117000, 117600, 117649, 117936, 
		118098, 118125, 118272, 118800, 119070, 119808, 120000, 120050, 120285, 120393, 
		120736, 120960, 121275, 121500, 121875, 122304, 122472, 122500, 122850, 122880, 
		123200, 123480, 123750, 124416, 124740, 124800, 124852, 125000, 125440, 126000, 
		126360, 126720, 127008, 127400, 127575, 128000, 128304, 128625, 129024, 129360, 
		129600, 129654, 130000, 130977, 131040, 131072, 131220, 131250, 131625, 131712, 
		132000, 132055, 132300, 132678, 133056, 133120, 133650, 133770, 134400, 134456, 
		134750, 134784, 135000, 135168, 135828, 136080, 136500, 137200, 137500, 137592, 
		137781, 137984, 138240, 138600, 138915, 139776, 139968, 140000, 140400, 140625, 
		140800, 141120, 141750, 142155, 142560, 142688, 142884, 143325, 143360, 144000, 
		144060, 144342, 144375, 145152, 145530, 145600, 145800, 146250, 147000, 147420, 
		147456, 147840, 148176, 148500, 149688, 149760, 150000, 150528, 150920, 151200, 
		151263, 151632, 151875, 152064, 152880, 153090, 153125, 153600, 153664, 154000, 
		154350, 154791, 155232, 155520, 155925, 156000, 156065, 156250, 156800, 157248, 
		157464, 157500, 157696, 157950, 158400, 158466, 158760, 159250, 159744, 160000, 
		160380, 160524, 161280, 161700, 162000, 162500, 163072, 163296, 163800, 163840, 
		164025, 164640, 165000, 165375, 165888, 166320, 166400, 166698, 168000, 168070, 
		168399, 168480, 168750, 168960, 169344, 169785, 170100, 170586, 170625, 171072, 
		171500, 171875, 171990, 172032, 172480, 172800, 172872, 173250, 174636, 174720, 
		174960, 175000, 175500, 175616, 176000, 176400, 176904, 177147, 177408, 178200, 
		178360, 178605, 179200, 179712, 180000, 180075, 180224, 181104, 181440, 182000, 
		182250, 183456, 183708, 183750, 184275, 184320, 184800, 184877, 185220, 185625, 
		186368, 186624, 187110, 187200, 187278, 187500, 188160, 188650, 189000, 189540, 
		190080, 190512, 191100, 192000, 192080, 192456, 192500, 193536, 194040, 194400, 
		194481, 195000, 196000, 196560, 196608, 196830, 196875, 197120, 197568, 198000, 
		198450, 199017, 199584, 199680, 200000, 200475, 200655, 200704, 201600, 201684, 
		202125, 202176, 202500, 202752, 203125, 203742, 203840, 204120, 204750, 204800, 
		205800, 206250, 206388, 206976, 207360, 207900, 208000, 209664, 209952, 210000, 
		210600, 211200, 211288, 211680, 212625, 212992, 213840, 214032, 214326, 214375, 
		215040, 215600, 216000, 216090, 216513, 217728, 218295, 218400, 218491, 218700, 
		218750, 219375, 219520, 220000, 220500, 221130, 221184, 221760, 222264, 222750, 
		222950, 224000, 224532, 224640, 225000, 225280, 225792, 226380, 226800, 227448, 
		227500, 228096, 229320, 229376, 229635, 230400, 230496, 231000, 231525, 232848, 
		232960, 233280, 234000, 234375, 235200, 235298, 235872, 236196, 236250, 236544, 
		236925, 237600, 237699, 238140, 238875, 239616, 240000, 240100, 240570, 240625, 
		240786, 241472, 241920, 242550, 243000, 243750, 244608, 244944, 245000, 245700, 
		245760, 246400, 246960, 247500, 248832, 249480, 249600, 249704, 250000, 250047, 
		250880, 252000, 252105, 252720, 253125, 253440, 254016, 254800, 255150, 255879, 
		256000, 256608, 257250, 257985, 258048, 258720, 259200, 259308, 259875, 260000, 
		261954, 262080, 262144, 262440, 262500, 263250, 263424, 264000, 264110, 264600, 
		265356, 266112, 266240, 267300, 267540, 268800, 268912, 269500, 269568, 270000, 
		270336, 271656, 272160, 273000, 273375, 274400, 275000, 275184, 275562, 275625, 
		275968, 276480, 277200, 277830, 279552, 279936, 280000, 280665, 280800, 280917, 
		281250, 281600, 282240, 282975, 283500, 284310, 284375, 285120, 285376, 285768, 
		286650, 286720, 288000, 288120, 288684, 288750, 290304, 291060, 291200, 291600, 
		292500, 294000, 294840, 294912, 295245, 295680, 296352, 297000, 297675, 299376, 
		299520, 300000, 300125, 301056, 301840, 302400, 302526, 303264, 303750, 304128, 
		305613, 305760, 306180, 306250, 307125, 307200, 307328, 308000, 308700, 309375, 
		309582, 310464, 311040, 311850, 312000, 312130, 312500, 313600, 314496, 314928, 
		315000, 315392, 315900, 316800, 316932, 317520, 318500, 319488, 320000, 320760, 
		321048, 321489, 322560, 323400, 324000, 324135, 325000, 326144, 326592, 327600, 
		327680, 328050, 328125, 329280, 330000, 330750, 331695, 331776, 332640, 332800, 
		333396, 334125, 334425, 336000, 336140, 336798, 336875, 336960, 337500, 337920, 
		338688, 339570, 340200, 341172, 341250, 342144, 343000, 343750, 343980, 344064, 
		344960, 345600, 345744, 346500, 349272, 349440, 349920, 350000, 351000, 351232, 
		352000, 352800, 352947, 353808, 354294, 354375, 354816, 356400, 356720, 357210, 
		358400, 359424, 360000, 360150, 360448, 360855, 361179, 362208, 362880, 363825, 
		364000, 364500, 365625, 366912, 367416, 367500, 368550, 368640, 369600, 369754, 
		370440, 371250, 372736, 373248, 374220, 374400, 374556, 375000, 376320, 377300, 
		378000, 379080, 380160, 381024, 382200, 382725, 384000, 384160, 384912, 385000, 
		385875, 387072, 388080, 388800, 388962, 390000, 390625, 392000, 392931, 393120, 
		393216, 393660, 393750, 394240, 394875, 395136, 396000, 396165, 396900, 398034, 
		398125, 399168, 399360, 400000, 400950, 401310, 401408, 403200, 403368, 404250, 
		404352, 405000, 405504, 406250, 407484, 407680, 408240, 409500, 409600, 411600, 
		412500, 412776, 413343, 413952, 414720, 415800, 416000, 416745, 419328, 419904, 
		420000, 420175, 421200, 421875, 422400, 422576, 423360, 425250, 425984, 426465, 
		427680, 428064, 428652, 428750, 429975, 430080, 431200, 432000, 432180, 433026, 
		433125, 435456, 436590, 436800, 436982, 437400, 437500, 438750, 439040, 440000, 
		441000, 442260, 442368, 443520, 444528, 445500, 445900, 448000, 449064, 449280, 
		450000, 450560, 451584, 452760, 453600, 453789, 454896, 455000, 455625, 456192, 
		458640, 458752, 459270, 459375, 460800, 460992, 462000, 463050, 464373, 465696, 
		465920, 466560, 467775, 468000, 468195, 468750, 470400, 470596, 471625, 471744, 
		472392, 472500, 473088, 473850, 475200, 475398, 476280, 477750, 479232, 480000, 
		480200, 481140, 481250, 481572, 482944, 483840, 485100, 486000, 487500, 489216, 
		489888, 490000, 491400, 491520, 492075, 492800, 493920, 495000, 496125, 497664, 
		498960, 499200, 499408, 500000, 500094, 501760, 504000, 504210, 505197, 505440, 
		506250, 506880, 508032, 509355, 509600, 510300, 511758, 511875, 512000, 513216, 
		514500, 515625, 515970, 516096, 517440, 518400, 518616, 519750, 520000, 523908, 
		524160, 524288, 524880, 525000, 526500, 526848, 528000, 528220, 529200, 530712, 
		531441, 532224, 532480, 534600, 535080, 535815, 537600, 537824, 539000, 539136, 
		540000, 540225, 540672, 543312, 544320, 546000, 546750, 546875, 548800, 550000, 
		550368, 551124, 551250, 551936, 552825, 552960, 554400, 554631, 555660, 556875, 
		557375, 559104, 559872, 560000, 561330, 561600, 561834, 562500, 563200, 564480, 
		565950, 567000, 568620, 568750, 570240, 570752, 571536, 573300, 573440, 576000, 
		576240, 577368, 577500, 580608, 582120, 582400, 583200, 583443, 585000, 588000, 
		588245, 589680, 589824, 590490, 590625, 591360, 592704, 594000, 595350, 597051, 
		598752, 599040, 600000, 600250, 601425, 601965, 602112, 603680, 604800, 605052, 
		606375, 606528, 607500, 608256, 609375, 611226, 611520, 612360, 612500, 614250, 
		614400, 614656, 616000, 617400, 618750, 619164, 620928, 622080, 623700, 624000, 
		624260, 625000, 627200, 628992, 629856, 630000, 630784, 631800, 633600, 633864, 
		635040, 637000, 637875, 638976, 640000, 641520, 642096, 642978, 643125, 645120, 
		646800, 648000, 648270, 649539, 650000, 652288, 653184, 654885, 655200, 655360, 
		655473, 656100, 656250, 658125, 658560, 660000, 660275, 661500, 663390, 663552, 
		665280, 665600, 666792, 668250, 668850, 672000, 672280, 673596, 673750, 673920, 
		675000, 675840, 677376, 679140, 680400, 682344, 682500, 684288, 686000, 687500, 
		687960, 688128, 688905, 689920, 691200, 691488, 693000, 694575, 698544, 698880, 
		699840, 700000, 702000, 702464, 703125, 704000, 705600, 705894, 707616, 708588, 
		708750, 709632, 710775, 712800, 713097, 713440, 714420, 716625, 716800, 718848, 
		720000, 720300, 720896, 721710, 721875, 722358, 724416, 725760, 727650, 728000, 
		729000, 731250, 733824, 734832, 735000, 737100, 737280, 739200, 739508, 740880, 
		742500, 745472, 746496, 748440, 748800, 749112, 750000, 750141, 752640, 754600, 
		756000, 756315, 758160, 759375, 760320, 762048, 764400, 765450, 765625, 767637, 
		768000, 768320, 769824, 770000, 771750, 773955, 774144, 776160, 777600, 777924, 
		779625, 780000, 780325, 781250, 784000, 785862, 786240, 786432, 787320, 787500, 
		788480, 789750, 790272, 792000, 792330, 793800, 796068, 796250, 798336, 798720, 
		800000, 801900, 802620, 802816, 806400, 806736, 808500, 808704, 810000, 811008, 
		812500, 814968, 815360, 816480, 819000, 819200, 820125, 823200, 823543, 825000, 
		825552, 826686, 826875, 827904, 829440, 831600, 832000, 833490, 838656, 839808, 
		840000, 840350, 841995, 842400, 842751, 843750, 844800, 845152, 846720, 848925, 
		850500, 851968, 852930, 853125, 855360, 856128, 857304, 857500, 859375, 859950, 
		860160, 862400, 864000, 864360, 866052, 866250, 870912, 873180, 873600, 873964, 
		874800, 875000, 877500, 878080, 880000, 882000, 884520, 884736, 885735, 887040, 
		889056, 891000, 891800, 893025, 896000, 898128, 898560, 900000, 900375, 901120, 
		903168, 905520, 907200, 907578, 909792, 910000, 911250, 912384, 916839, 917280, 
		917504, 918540, 918750, 921375, 921600, 921984, 924000, 924385, 926100, 928125, 
		928746, 931392, 931840, 933120, 935550, 936000, 936390, 937500, 940800, 941192, 
		943250, 943488, 944784, 945000, 946176, 947700, 950400, 950796, 952560, 955500, 
		958464, 960000, 960400, 962280, 962500, 963144, 964467, 965888, 967680, 970200, 
		972000, 972405, 975000, 978432, 979776, 980000, 982800, 983040, 984150, 984375, 
		985600, 987840, 990000, 992250, 995085, 995328, 997920, 998400, 998816, 1000000
};



DWORD OptimalDFT::GetOptimalDFTSize( DWORD size0 )
{
	if(size0 >= HalfLargestDFTSize)
		throw convolutionException("Convolution too big to handle");
#ifdef FFTW
	DWORD a = 0;
	DWORD b = sizeof(OptimalDFTSize)/sizeof(OptimalDFTSize[0]) - 1;
	assert( (unsigned)size0 < (unsigned)OptimalDFTSize[b] );


	while( a < b )
	{
		DWORD c = (a + b) >> 1;
		if( size0 <= OptimalDFTSize[c] )
			b = c;
		else
			a = c+1;
	}

	return OptimalDFTSize[b];
#else
	// highest power of two greater than size0
	DWORD d = 2;
	while(d < size0)
	{
		d*=2;
	}
	return d;
#endif
}