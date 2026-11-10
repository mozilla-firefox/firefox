/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "nsITransactionManager.h"
#include "nsComponentManagerUtils.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/EditTransactionBase.h"
#include "mozilla/TransactionManager.h"

using mozilla::EditTransactionBase;
using mozilla::TransactionManager;

static int32_t sConstructorCount = 0;
static int32_t sDoCount = 0;
static int32_t* sDoOrderArr = nullptr;
static int32_t sUndoCount = 0;
static int32_t* sUndoOrderArr = nullptr;
static int32_t sRedoCount = 0;
static int32_t* sRedoOrderArr = nullptr;

int32_t sSimpleTestDoOrderArr[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
    31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,
    46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
    61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
    76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
    91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
    106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
    121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131};

int32_t sSimpleTestUndoOrderArr[] = {41,  40,  39,  38,  62,  39,  38,  37,
                                     69,  71,  70,  111, 110, 109, 108, 107,
                                     106, 105, 104, 103, 102, 131, 130, 129,
                                     128, 127, 126, 125, 124, 123, 122};

static int32_t sSimpleTestRedoOrderArr[] = {38, 39, 70};

int32_t sAggregateTestDoOrderArr[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
    31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,
    46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
    61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
    76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
    91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
    106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
    121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150,
    151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165,
    166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180,
    181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195,
    196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
    211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
    241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
    256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270,
    271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285,
    286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300,
    301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315,
    316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330,
    331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345,
    346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360,
    361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375,
    376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390,
    391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405,
    406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420,
    421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435,
    436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450,
    451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465,
    466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
    496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510,
    511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525,
    526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540,
    541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555,
    556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570,
    571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585,
    586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 600,
    601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615,
    616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630,
    631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645,
    646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659, 660,
    661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674, 675,
    676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689, 690,
    691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704, 705,
    706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719, 720,
    721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 735,
    736, 737, 738, 739, 740, 741, 742, 743, 744, 745, 746, 747, 748, 749, 750,
    751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761, 762, 763, 764, 765,
    766, 767, 768, 769, 770, 771, 772, 773, 774, 775, 776, 777, 778, 779, 780,
    781, 782, 783, 784, 785, 786, 787, 788, 789, 790, 791, 792, 793, 794, 795,
    796, 797, 798, 799, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809, 810,
    811, 812, 813, 814, 815, 816, 817, 818, 819, 820, 821, 822, 823, 824, 825,
    826, 827, 828, 829, 830, 831, 832, 833, 834, 835, 836, 837, 838, 839, 840,
    841, 842, 843, 844, 845, 846, 847, 848, 849, 850, 851, 852, 853, 854, 855,
    856, 857, 858, 859, 860, 861, 862, 863, 864, 865, 866, 867, 868, 869, 870,
    871, 872, 873, 874, 875, 876, 877, 878, 879, 880, 881, 882, 883, 884, 885,
    886, 887, 888, 889, 890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900,
    901, 902, 903, 904, 905, 906, 907, 908, 909, 910, 911, 912, 913};

int32_t sAggregateTestUndoOrderArr[] = {
    287, 286, 285, 284, 283, 282, 281, 280, 279, 278, 277, 276, 275, 274, 273,
    272, 271, 270, 269, 268, 267, 266, 265, 264, 263, 262, 261, 260, 434, 433,
    432, 431, 430, 429, 428, 273, 272, 271, 270, 269, 268, 267, 266, 265, 264,
    263, 262, 261, 260, 259, 258, 257, 256, 255, 254, 253, 479, 478, 477, 476,
    475, 493, 492, 491, 490, 489, 488, 487, 486, 485, 484, 483, 482, 481, 480,
    485, 484, 483, 482, 481, 480, 773, 772, 771, 770, 769, 768, 767, 766, 765,
    764, 763, 762, 761, 760, 759, 758, 757, 756, 755, 754, 753, 752, 751, 750,
    749, 748, 747, 746, 745, 744, 743, 742, 741, 740, 739, 738, 737, 736, 735,
    734, 733, 732, 731, 730, 729, 728, 727, 726, 725, 724, 723, 722, 721, 720,
    719, 718, 717, 716, 715, 714, 713, 712, 711, 710, 709, 708, 707, 706, 705,
    704, 913, 912, 911, 910, 909, 908, 907, 906, 905, 904, 903, 902, 901, 900,
    899, 898, 897, 896, 895, 894, 893, 892, 891, 890, 889, 888, 887, 886, 885,
    884, 883, 882, 881, 880, 879, 878, 877, 876, 875, 874, 873, 872, 871, 870,
    869, 868, 867, 866, 865, 864, 863, 862, 861, 860, 859, 858, 857, 856, 855,
    854, 853, 852, 851, 850, 849, 848, 847, 846, 845, 844};

int32_t sAggregateTestRedoOrderArr[] = {
    260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272,
    273, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486};

int32_t sSimpleBatchTestDoOrderArr[] = {
    1,  2,  3,  4,   5,   6,   7,   8,   9,   10,  11, 12, 13, 14, 15, 16,
    17, 18, 19, 20,  21,  22,  23,  24,  25,  26,  27, 28, 29, 30, 31, 32,
    33, 34, 35, 36,  37,  38,  39,  40,  41,  42,  43, 44, 45, 46, 47, 48,
    49, 50, 51, 52,  53,  54,  55,  56,  57,  58,  59, 60, 61, 62, 63, 64,
    65, 66, 67, 68,  69,  70,  71,  72,  73,  74,  75, 76, 77, 78, 79, 80,
    81, 82, 83, 84,  85,  86,  87,  88,  89,  90,  91, 92, 93, 94, 95, 96,
    97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107};

int32_t sSimpleBatchTestUndoOrderArr[] = {
    43, 42, 41, 20, 19,  18,  17,  16,  15,  14,  13,  12,  11, 10, 9,
    8,  7,  6,  5,  4,   3,   2,   1,   43,  42,  41,  63,  62, 61, 60,
    59, 58, 57, 56, 55,  54,  53,  52,  51,  50,  49,  48,  47, 46, 45,
    44, 65, 67, 66, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98};

int32_t sSimpleBatchTestRedoOrderArr[] = {1,  2,  3,  4,  5,  6,  7,  8,
                                          9,  10, 11, 12, 13, 14, 15, 16,
                                          17, 18, 19, 20, 41, 42, 43, 66};

int32_t sAggregateBatchTestDoOrderArr[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
    31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,
    46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
    61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
    76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
    91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
    106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
    121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150,
    151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165,
    166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180,
    181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195,
    196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
    211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
    241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
    256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270,
    271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285,
    286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300,
    301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315,
    316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330,
    331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345,
    346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360,
    361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375,
    376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390,
    391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405,
    406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420,
    421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435,
    436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450,
    451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465,
    466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
    496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510,
    511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525,
    526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540,
    541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555,
    556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570,
    571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585,
    586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 600,
    601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615,
    616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630,
    631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645,
    646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659, 660,
    661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674, 675,
    676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689, 690,
    691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704, 705,
    706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719, 720,
    721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 735,
    736, 737, 738, 739, 740, 741, 742, 743, 744, 745};

int32_t sAggregateBatchTestUndoOrderArr[] = {
    301, 300, 299, 298, 297, 296, 295, 294, 293, 292, 291, 290, 289, 288, 287,
    286, 285, 284, 283, 282, 281, 140, 139, 138, 137, 136, 135, 134, 133, 132,
    131, 130, 129, 128, 127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117,
    116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102,
    101, 100, 99,  98,  97,  96,  95,  94,  93,  92,  91,  90,  89,  88,  87,
    86,  85,  84,  83,  82,  81,  80,  79,  78,  77,  76,  75,  74,  73,  72,
    71,  70,  69,  68,  67,  66,  65,  64,  63,  62,  61,  60,  59,  58,  57,
    56,  55,  54,  53,  52,  51,  50,  49,  48,  47,  46,  45,  44,  43,  42,
    41,  40,  39,  38,  37,  36,  35,  34,  33,  32,  31,  30,  29,  28,  27,
    26,  25,  24,  23,  22,  21,  20,  19,  18,  17,  16,  15,  14,  13,  12,
    11,  10,  9,   8,   7,   6,   5,   4,   3,   2,   1,   301, 300, 299, 298,
    297, 296, 295, 294, 293, 292, 291, 290, 289, 288, 287, 286, 285, 284, 283,
    282, 281, 441, 440, 439, 438, 437, 436, 435, 434, 433, 432, 431, 430, 429,
    428, 427, 426, 425, 424, 423, 422, 421, 420, 419, 418, 417, 416, 415, 414,
    413, 412, 411, 410, 409, 408, 407, 406, 405, 404, 403, 402, 401, 400, 399,
    398, 397, 396, 395, 394, 393, 392, 391, 390, 389, 388, 387, 386, 385, 384,
    383, 382, 381, 380, 379, 378, 377, 376, 375, 374, 373, 372, 371, 370, 369,
    368, 367, 366, 365, 364, 363, 362, 361, 360, 359, 358, 357, 356, 355, 354,
    353, 352, 351, 350, 349, 348, 347, 346, 345, 344, 343, 342, 341, 340, 339,
    338, 337, 336, 335, 334, 333, 332, 331, 330, 329, 328, 327, 326, 325, 324,
    323, 322, 321, 320, 319, 318, 317, 316, 315, 314, 313, 312, 311, 310, 309,
    308, 307, 306, 305, 304, 303, 302, 451, 450, 449, 448, 447, 465, 464, 463,
    462, 461, 460, 459, 458, 457, 456, 455, 454, 453, 452, 457, 456, 455, 454,
    453, 452, 745, 744, 743, 742, 741, 740, 739, 738, 737, 736, 735, 734, 733,
    732, 731, 730, 729, 728, 727, 726, 725, 724, 723, 722, 721, 720, 719, 718,
    717, 716, 715, 714, 713, 712, 711, 710, 709, 708, 707, 706, 705, 704, 703,
    702, 701, 700, 699, 698, 697, 696, 695, 694, 693, 692, 691, 690, 689, 688,
    687, 686, 685, 684, 683, 682, 681, 680, 679, 678, 677, 676};

int32_t sAggregateBatchTestRedoOrderArr[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
    31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,
    46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
    61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
    76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
    91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
    106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
    121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 138, 139, 140, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290,
    291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 448, 449, 450, 451,
    452, 453, 454, 455, 456, 457, 458};

class TestTransaction : public nsITransaction {
 protected:
  virtual ~TestTransaction() = default;

 public:
  TestTransaction() {}

  NS_DECL_ISUPPORTS

  NS_IMETHOD GetAsEditTransactionBase(EditTransactionBase**) final {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
};

NS_IMPL_ISUPPORTS(TestTransaction, nsITransaction)

class SimpleTransaction : public TestTransaction {
 protected:
#define NONE_FLAG 0
#define THROWS_DO_ERROR_FLAG 1
#define THROWS_UNDO_ERROR_FLAG 2
#define THROWS_REDO_ERROR_FLAG 4
#define MERGE_FLAG 8
#define TRANSIENT_FLAG 16
#define BATCH_FLAG 32
#define ALL_ERROR_FLAGS \
  (THROWS_DO_ERROR_FLAG | THROWS_UNDO_ERROR_FLAG | THROWS_REDO_ERROR_FLAG)

  int32_t mVal;
  int32_t mFlags;

 public:
  explicit SimpleTransaction(int32_t aFlags = NONE_FLAG)
      : mVal(++sConstructorCount), mFlags(aFlags) {}

  ~SimpleTransaction() override = default;

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD DoTransaction() override {
    //
    // Make sure DoTransaction() is called in the order we expect!
    // Notice that we don't check to see if we go past the end of the array.
    // This is done on purpose since we want to crash if the order array is out
    // of date.
    //
    if (sDoOrderArr) {
      EXPECT_EQ(mVal, sDoOrderArr[sDoCount]);
    }

    ++sDoCount;

    return (mFlags & THROWS_DO_ERROR_FLAG) ? NS_ERROR_FAILURE : NS_OK;
  }

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD UndoTransaction() override {
    //
    // Make sure UndoTransaction() is called in the order we expect!
    // Notice that we don't check to see if we go past the end of the array.
    // This is done on purpose since we want to crash if the order array is out
    // of date.
    //
    if (sUndoOrderArr) {
      EXPECT_EQ(mVal, sUndoOrderArr[sUndoCount]);
    }

    ++sUndoCount;

    return (mFlags & THROWS_UNDO_ERROR_FLAG) ? NS_ERROR_FAILURE : NS_OK;
  }

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD RedoTransaction() override {
    //
    // Make sure RedoTransaction() is called in the order we expect!
    // Notice that we don't check to see if we go past the end of the array.
    // This is done on purpose since we want to crash if the order array is out
    // of date.
    //
    if (sRedoOrderArr) {
      EXPECT_EQ(mVal, sRedoOrderArr[sRedoCount]);
    }

    ++sRedoCount;

    return (mFlags & THROWS_REDO_ERROR_FLAG) ? NS_ERROR_FAILURE : NS_OK;
  }

  NS_IMETHOD GetIsTransient(bool* aIsTransient) override {
    if (aIsTransient) {
      *aIsTransient = (mFlags & TRANSIENT_FLAG) ? true : false;
    }
    return NS_OK;
  }

  NS_IMETHOD Merge(nsITransaction* /*aTransaction*/, bool* aDidMerge) override {
    if (aDidMerge) {
      *aDidMerge = (mFlags & MERGE_FLAG) ? true : false;
    }
    return NS_OK;
  }
};

class AggregateTransaction : public SimpleTransaction {
 private:
  AggregateTransaction(nsITransactionManager* aTXMgr, int32_t aLevel,
                       int32_t aNumber, int32_t aMaxLevel,
                       int32_t aNumChildrenPerNode, int32_t aFlags) {
    mLevel = aLevel;
    mNumber = aNumber;
    mTXMgr = aTXMgr;
    mFlags = aFlags & (~ALL_ERROR_FLAGS);
    mErrorFlags = aFlags & ALL_ERROR_FLAGS;
    mTXMgr = aTXMgr;
    mMaxLevel = aMaxLevel;
    mNumChildrenPerNode = aNumChildrenPerNode;
  }

  nsITransactionManager* mTXMgr;

  int32_t mLevel;
  int32_t mNumber;
  int32_t mErrorFlags;

  int32_t mMaxLevel;
  int32_t mNumChildrenPerNode;

 public:
  AggregateTransaction(nsITransactionManager* aTXMgr, int32_t aMaxLevel,
                       int32_t aNumChildrenPerNode,
                       int32_t aFlags = NONE_FLAG) {
    mLevel = 1;
    mNumber = 1;
    mFlags = aFlags & (~ALL_ERROR_FLAGS);
    mErrorFlags = aFlags & ALL_ERROR_FLAGS;
    mTXMgr = aTXMgr;
    mMaxLevel = aMaxLevel;
    mNumChildrenPerNode = aNumChildrenPerNode;
  }

  ~AggregateTransaction() override = default;

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD DoTransaction() override {
    if (mLevel >= mMaxLevel) {
      // Only leaf nodes can throw errors!
      mFlags |= mErrorFlags;
    }

    nsresult rv = SimpleTransaction::DoTransaction();
    if (NS_FAILED(rv)) {
      // fail("QueryInterface() failed for transaction level %d. (%d)\n",
      //      mLevel, rv);
      return rv;
    }

    if (mLevel >= mMaxLevel) {
      return NS_OK;
    }

    if (mFlags & BATCH_FLAG) {
      rv = MOZ_KnownLive(mTXMgr)->BeginBatch(nullptr);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }

    int32_t cLevel = mLevel + 1;

    for (int i = 1; i <= mNumChildrenPerNode; i++) {
      int32_t flags = mErrorFlags & THROWS_DO_ERROR_FLAG;

      if ((mErrorFlags & THROWS_REDO_ERROR_FLAG) && i == mNumChildrenPerNode) {
        // Make the rightmost leaf transaction throw the error!
        flags = THROWS_REDO_ERROR_FLAG;
        mErrorFlags = mErrorFlags & (~THROWS_REDO_ERROR_FLAG);
      } else if ((mErrorFlags & THROWS_UNDO_ERROR_FLAG) && i == 1) {
        // Make the leftmost leaf transaction throw the error!
        flags = THROWS_UNDO_ERROR_FLAG;
        mErrorFlags = mErrorFlags & (~THROWS_UNDO_ERROR_FLAG);
      }

      flags |= mFlags & BATCH_FLAG;

      RefPtr<AggregateTransaction> tximpl = new AggregateTransaction(
          mTXMgr, cLevel, i, mMaxLevel, mNumChildrenPerNode, flags);

      rv = MOZ_KnownLive(mTXMgr)->DoTransaction(tximpl);
      if (NS_FAILED(rv)) {
        if (mFlags & BATCH_FLAG) {
          mTXMgr->EndBatch(false);
        }
        return rv;
      }
    }

    if (mFlags & BATCH_FLAG) {
      mTXMgr->EndBatch(false);
    }
    return rv;
  }
};

class TestTransactionFactory {
 public:
  virtual TestTransaction* create(nsITransactionManager* txmgr,
                                  int32_t flags) = 0;
};

class SimpleTransactionFactory : public TestTransactionFactory {
 public:
  TestTransaction* create(nsITransactionManager* /*txmgr*/,
                          int32_t flags) override {
    return (TestTransaction*)new SimpleTransaction(flags);
  }
};

class AggregateTransactionFactory : public TestTransactionFactory {
 private:
  int32_t mMaxLevel;
  int32_t mNumChildrenPerNode;
  int32_t mFixedFlags;

 public:
  AggregateTransactionFactory(int32_t aMaxLevel, int32_t aNumChildrenPerNode,
                              int32_t aFixedFlags = NONE_FLAG)
      : mMaxLevel(aMaxLevel),
        mNumChildrenPerNode(aNumChildrenPerNode),
        mFixedFlags(aFixedFlags) {}

  TestTransaction* create(nsITransactionManager* txmgr,
                          int32_t flags) override {
    return (TestTransaction*)new AggregateTransaction(
        txmgr, mMaxLevel, mNumChildrenPerNode, flags | mFixedFlags);
  }
};

void reset_globals() {
  sConstructorCount = 0;

  sDoCount = 0;
  sDoOrderArr = nullptr;

  sUndoCount = 0;
  sUndoOrderArr = nullptr;

  sRedoCount = 0;
  sRedoOrderArr = nullptr;
}

/**
 * Test behaviors in non-batch mode.
 **/
MOZ_CAN_RUN_SCRIPT_BOUNDARY void quick_test(TestTransactionFactory* factory) {
  /*******************************************************************
   *
   * Create a transaction manager implementation:
   *
   *******************************************************************/

  nsCOMPtr<nsITransactionManager> mgr = new TransactionManager();

  /*******************************************************************
   *
   * Call DoTransaction() with a null transaction:
   *
   *******************************************************************/

  nsresult rv = mgr->DoTransaction(nullptr);
  EXPECT_EQ(rv, NS_ERROR_NULL_POINTER);

  /*******************************************************************
   *
   * Call UndoTransaction() with an empty undo stack:
   *
   *******************************************************************/

  rv = mgr->UndoTransaction();
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call RedoTransaction() with an empty redo stack:
   *
   *******************************************************************/

  rv = mgr->RedoTransaction();
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call SetMaxTransactionCount(-1) with empty undo and redo stacks:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(-1);
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call SetMaxTransactionCount(0) with empty undo and redo stacks:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(0);
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call SetMaxTransactionCount(10) with empty undo and redo stacks:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(10);
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call Clear() with empty undo and redo stacks:
   *
   *******************************************************************/

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Call GetNumberOfUndoItems() with an empty undo stack:
   *
   *******************************************************************/

  int32_t numitems;
  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Call GetNumberOfRedoItems() with an empty redo stack:
   *
   *******************************************************************/

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Call PeekUndoStack() with an empty undo stack:
   *
   *******************************************************************/

  {
    nsCOMPtr<nsITransaction> tx;
    rv = mgr->PeekUndoStack(getter_AddRefs(tx));
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(tx, nullptr);
  }

  /*******************************************************************
   *
   * Call PeekRedoStack() with an empty undo stack:
   *
   *******************************************************************/

  {
    nsCOMPtr<nsITransaction> tx;
    rv = mgr->PeekRedoStack(getter_AddRefs(tx));
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(tx, nullptr);
  }

  /*******************************************************************
   *
   * Test coalescing by executing a transaction that can merge any
   * command into itself. Then execute 20 transaction. Afterwards,
   * we should still have the first transaction sitting on the undo
   * stack. Then clear the undo and redo stacks.
   *
   *******************************************************************/

  int32_t i;
  RefPtr<TestTransaction> tximpl;
  nsCOMPtr<nsITransaction> u1, u2, r1, r2;

  rv = mgr->SetMaxTransactionCount(10);
  EXPECT_NS_SUCCEEDED(rv);

  tximpl = factory->create(mgr, MERGE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, tximpl);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);

  /*******************************************************************
   *
   * Execute 20 transactions. Afterwards, we should have 10
   * transactions on the undo stack:
   *
   *******************************************************************/

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Execute 20 transient transactions. Afterwards, we should still
   * have the same 10 transactions on the undo stack:
   *
   *******************************************************************/

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, TRANSIENT_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Undo 4 transactions. Afterwards, we should have 6 transactions
   * on the undo stack, and 4 on the redo stack:
   *
   *******************************************************************/

  for (i = 1; i <= 4; i++) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 6);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 4);

  /*******************************************************************
   *
   * Redo 2 transactions. Afterwards, we should have 8 transactions
   * on the undo stack, and 2 on the redo stack:
   *
   *******************************************************************/

  for (i = 1; i <= 2; ++i) {
    rv = mgr->RedoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 8);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  /*******************************************************************
   *
   * Execute a new transaction. The redo stack should get pruned!
   *
   *******************************************************************/

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 9);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Undo 4 transactions then clear the undo and redo stacks.
   *
   *******************************************************************/

  for (i = 1; i <= 4; ++i) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 5);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 4);

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Execute 5 transactions.
   *
   *******************************************************************/

  for (i = 1; i <= 5; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 5);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Test transaction DoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_DO_ERROR_FLAG);

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->DoTransaction(tximpl);
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 5);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Test transaction UndoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_UNDO_ERROR_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->UndoTransaction();
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 6);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Test transaction RedoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_REDO_ERROR_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  //
  // Execute a normal transaction to be used in a later test:
  //

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  //
  // Undo the 2 transactions just executed.
  //

  for (i = 1; i <= 2; ++i) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  //
  // The RedoErrorTransaction should now be at the top of the redo stack!
  //

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->RedoTransaction();
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 6);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  /*******************************************************************
   *
   * Make sure that setting the transaction manager's max transaction
   * count to zero, clears both the undo and redo stacks, and executes
   * all new commands without pushing them on the undo stack!
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(0);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->GetNumberOfUndoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);

    rv = mgr->GetNumberOfRedoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);
  }

  /*******************************************************************
   *
   * Make sure that setting the transaction manager's max transaction
   * count to something greater than the number of transactions on
   * both the undo and redo stacks causes no pruning of the stacks:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(-1);
  EXPECT_NS_SUCCEEDED(rv);

  // Push 20 transactions on the undo stack:

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->GetNumberOfUndoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, i);

    rv = mgr->GetNumberOfRedoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);
  }

  for (i = 1; i <= 10; i++) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }
  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->SetMaxTransactionCount(25);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  /*******************************************************************
   *
   * Test undo stack pruning by setting the transaction
   * manager's max transaction count to a number lower than the
   * number of transactions on both the undo and redo stacks:
   *
   *******************************************************************/

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->SetMaxTransactionCount(15);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 5);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  /*******************************************************************
   *
   * Test redo stack pruning by setting the transaction
   * manager's max transaction count to a number lower than the
   * number of transactions on both the undo and redo stacks:
   *
   *******************************************************************/

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->SetMaxTransactionCount(5);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_FALSE(u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 5);

  /*******************************************************************
   *
   * Release the transaction manager. Any transactions on the undo
   * and redo stack should automatically be released:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(-1);
  EXPECT_NS_SUCCEEDED(rv);

  // Push 20 transactions on the undo stack:

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->GetNumberOfUndoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, i);

    rv = mgr->GetNumberOfRedoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);
  }

  for (i = 1; i <= 10; i++) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);
}

TEST(TestTXMgr, SimpleTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/
  reset_globals();
  sDoOrderArr = sSimpleTestDoOrderArr;
  sUndoOrderArr = sSimpleTestUndoOrderArr;
  sRedoOrderArr = sSimpleTestRedoOrderArr;

  /*******************************************************************
   *
   * Run the quick test.
   *
   *******************************************************************/

  SimpleTransactionFactory factory;

  quick_test(&factory);
}

TEST(TestTXMgr, AggregationTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/

  reset_globals();
  sDoOrderArr = sAggregateTestDoOrderArr;
  sUndoOrderArr = sAggregateTestUndoOrderArr;
  sRedoOrderArr = sAggregateTestRedoOrderArr;

  /*******************************************************************
   *
   * Run the quick test.
   *
   *******************************************************************/

  AggregateTransactionFactory factory(3, 2);

  quick_test(&factory);
}

/**
 * Test behaviors in batch mode.
 **/
MOZ_CAN_RUN_SCRIPT_BOUNDARY void quick_batch_test(
    TestTransactionFactory* factory) {
  /*******************************************************************
   *
   * Create a transaction manager implementation:
   *
   *******************************************************************/

  nsCOMPtr<nsITransactionManager> mgr = new TransactionManager();

  int32_t numitems;

  /*******************************************************************
   *
   * Make sure an unbalanced call to EndBatch(false) with empty undo stack
   * throws an error!
   *
   *******************************************************************/

  nsresult rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->EndBatch(false);
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Make sure that an empty batch is not added to the undo stack
   * when it is closed.
   *
   *******************************************************************/

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  int32_t i;
  RefPtr<TestTransaction> tximpl;

  /*******************************************************************
   *
   * Execute 20 transactions. Afterwards, we should have 1
   * transaction on the undo stack:
   *
   *******************************************************************/

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  nsCOMPtr<nsITransaction> u1, u2, r1, r2;

  /*******************************************************************
   *
   * Execute 20 transient transactions. Afterwards, we should still
   * have the same transaction on the undo stack:
   *
   *******************************************************************/

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, TRANSIENT_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Test nested batching. Afterwards, we should have 2 transactions
   * on the undo stack:
   *
   *******************************************************************/

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  /*******************************************************************
   *
   * Undo 2 batch transactions. Afterwards, we should have 0
   * transactions on the undo stack and 2 on the redo stack.
   *
   *******************************************************************/

  for (i = 1; i <= 2; ++i) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  /*******************************************************************
   *
   * Redo 2 batch transactions. Afterwards, we should have 2
   * transactions on the undo stack and 0 on the redo stack.
   *
   *******************************************************************/

  for (i = 1; i <= 2; ++i) {
    rv = mgr->RedoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Call undo. Afterwards, we should have 1 transaction
   * on the undo stack, and 1 on the redo stack:
   *
   *******************************************************************/

  rv = mgr->UndoTransaction();
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  /*******************************************************************
   *
   * Make sure an unbalanced call to EndBatch(false) throws an error and
   * doesn't affect the undo and redo stacks!
   *
   *******************************************************************/

  rv = mgr->EndBatch(false);
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  /*******************************************************************
   *
   * Make sure that an empty batch is not added to the undo stack
   * when it is closed, and that it does not affect the undo and redo
   * stacks.
   *
   *******************************************************************/

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  /*******************************************************************
   *
   * Execute a new transaction. The redo stack should get pruned!
   *
   *******************************************************************/

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);
    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);
  }

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Call undo.
   *
   *******************************************************************/

  // Move a transaction over to the redo stack, so that we have one
  // transaction on the undo stack, and one on the redo stack!

  rv = mgr->UndoTransaction();
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  /*******************************************************************
   *
   * Test transaction DoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_DO_ERROR_FLAG);

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->DoTransaction(tximpl);
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 1);

  /*******************************************************************
   *
   * Test transaction UndoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_UNDO_ERROR_FLAG);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->UndoTransaction();
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  /*******************************************************************
   *
   * Test transaction RedoTransaction() error:
   *
   *******************************************************************/

  tximpl = factory->create(mgr, THROWS_REDO_ERROR_FLAG);

  rv = mgr->BeginBatch(nullptr);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->EndBatch(false);
  EXPECT_NS_SUCCEEDED(rv);

  //
  // Execute a normal transaction to be used in a later test:
  //

  tximpl = factory->create(mgr, NONE_FLAG);
  rv = mgr->DoTransaction(tximpl);
  EXPECT_NS_SUCCEEDED(rv);

  //
  // Undo the 2 transactions just executed.
  //

  for (i = 1; i <= 2; ++i) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }

  //
  // The RedoErrorTransaction should now be at the top of the redo stack!
  //

  u1 = u2 = r1 = r2 = nullptr;

  rv = mgr->PeekUndoStack(getter_AddRefs(u1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->PeekRedoStack(getter_AddRefs(r1));
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->RedoTransaction();
  EXPECT_EQ(rv, NS_ERROR_FAILURE);

  rv = mgr->PeekUndoStack(getter_AddRefs(u2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(u1, u2);

  rv = mgr->PeekRedoStack(getter_AddRefs(r2));
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(r1, r2);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 2);

  /*******************************************************************
   *
   * Make sure that setting the transaction manager's max transaction
   * count to zero, clears both the undo and redo stacks, and executes
   * all new commands without pushing them on the undo stack!
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(0);
  EXPECT_NS_SUCCEEDED(rv);

  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 0);

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);

    rv = mgr->BeginBatch(nullptr);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->EndBatch(false);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->GetNumberOfUndoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);

    rv = mgr->GetNumberOfRedoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);
  }

  /*******************************************************************
   *
   * Release the transaction manager. Any transactions on the undo
   * and redo stack should automatically be released:
   *
   *******************************************************************/

  rv = mgr->SetMaxTransactionCount(-1);
  EXPECT_NS_SUCCEEDED(rv);

  // Push 20 transactions on the undo stack:

  for (i = 1; i <= 20; i++) {
    tximpl = factory->create(mgr, NONE_FLAG);

    rv = mgr->BeginBatch(nullptr);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->DoTransaction(tximpl);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->EndBatch(false);
    EXPECT_NS_SUCCEEDED(rv);

    rv = mgr->GetNumberOfUndoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, i);

    rv = mgr->GetNumberOfRedoItems(&numitems);
    EXPECT_NS_SUCCEEDED(rv);
    EXPECT_EQ(numitems, 0);
  }

  for (i = 1; i <= 10; i++) {
    rv = mgr->UndoTransaction();
    EXPECT_NS_SUCCEEDED(rv);
  }
  rv = mgr->GetNumberOfUndoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->GetNumberOfRedoItems(&numitems);
  EXPECT_NS_SUCCEEDED(rv);
  EXPECT_EQ(numitems, 10);

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);
}

TEST(TestTXMgr, SimpleBatchTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/
  reset_globals();
  sDoOrderArr = sSimpleBatchTestDoOrderArr;
  sUndoOrderArr = sSimpleBatchTestUndoOrderArr;
  sRedoOrderArr = sSimpleBatchTestRedoOrderArr;

  /*******************************************************************
   *
   * Run the quick batch test.
   *
   *******************************************************************/

  SimpleTransactionFactory factory;
  quick_batch_test(&factory);
}

TEST(TestTXMgr, AggregationBatchTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/

  reset_globals();
  sDoOrderArr = sAggregateBatchTestDoOrderArr;
  sUndoOrderArr = sAggregateBatchTestUndoOrderArr;
  sRedoOrderArr = sAggregateBatchTestRedoOrderArr;

  /*******************************************************************
   *
   * Run the quick batch test.
   *
   *******************************************************************/

  AggregateTransactionFactory factory(3, 2, BATCH_FLAG);

  quick_batch_test(&factory);
}

/**
 * Create 'iterations * (iterations + 1) / 2' transactions;
 * do/undo/redo/undo them.
 **/
MOZ_CAN_RUN_SCRIPT_BOUNDARY void stress_test(TestTransactionFactory* factory,
                                             int32_t iterations) {
  /*******************************************************************
   *
   * Create a transaction manager:
   *
   *******************************************************************/

  nsCOMPtr<nsITransactionManager> mgr = new TransactionManager();

  nsresult rv;
  int32_t i, j;

  for (i = 1; i <= iterations; i++) {
    /*******************************************************************
     *
     * Create and execute a bunch of transactions:
     *
     *******************************************************************/

    for (j = 1; j <= i; j++) {
      RefPtr<TestTransaction> tximpl = factory->create(mgr, NONE_FLAG);
      rv = mgr->DoTransaction(tximpl);
      EXPECT_NS_SUCCEEDED(rv);
    }

    /*******************************************************************
     *
     * Undo all the transactions:
     *
     *******************************************************************/

    for (j = 1; j <= i; j++) {
      rv = mgr->UndoTransaction();
      EXPECT_NS_SUCCEEDED(rv);
    }

    /*******************************************************************
     *
     * Redo all the transactions:
     *
     *******************************************************************/

    for (j = 1; j <= i; j++) {
      rv = mgr->RedoTransaction();
      EXPECT_NS_SUCCEEDED(rv);
    }

    /*******************************************************************
     *
     * Undo all the transactions again so that they all end up on
     * the redo stack for pruning the next time we execute a new
     * transaction
     *
     *******************************************************************/

    for (j = 1; j <= i; j++) {
      rv = mgr->UndoTransaction();
      EXPECT_NS_SUCCEEDED(rv);
    }
  }

  rv = mgr->Clear();
  EXPECT_NS_SUCCEEDED(rv);
}

TEST(TestTXMgr, SimpleStressTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/

  reset_globals();

  /*******************************************************************
   *
   * Do the stress test:
   *
   *******************************************************************/

  SimpleTransactionFactory factory;

  int32_t iterations =
#ifdef DEBUG
      10
#else
      //
      // 1500 iterations sends 1,125,750 transactions through the system!!
      //
      1500
#endif
      ;
  stress_test(&factory, iterations);
}

TEST(TestTXMgr, AggregationStressTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/

  reset_globals();

  /*******************************************************************
   *
   * Do the stress test:
   *
   *******************************************************************/

  AggregateTransactionFactory factory(3, 4);

  int32_t iterations =
#ifdef DEBUG
      10
#else
      //
      // 500 iterations sends 2,630,250 transactions through the system!!
      //
      500
#endif
      ;
  stress_test(&factory, iterations);
}

TEST(TestTXMgr, AggregationBatchStressTest)
{
  /*******************************************************************
   *
   * Initialize globals for test.
   *
   *******************************************************************/

  reset_globals();

  /*******************************************************************
   *
   * Do the stress test:
   *
   *******************************************************************/

  AggregateTransactionFactory factory(3, 4, BATCH_FLAG);

  int32_t iterations =
#ifdef DEBUG
      10
#else
#  if defined(MOZ_ASAN) || defined(MOZ_WIDGET_ANDROID)
      // See Bug 929985: 500 is too many for ASAN and Android, 100 is safe.
      100
#  else
      //
      // 500 iterations sends 2,630,250 transactions through the system!!
      //
      500
#  endif
#endif
      ;
  stress_test(&factory, iterations);
}
