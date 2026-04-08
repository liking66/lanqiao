#include "intrins.h"
#include <stc15.h>


typedef unsigned int u16;
typedef unsigned char u8;

/*************** 数码管段码 ***************/
const unsigned char tab[] = {
    0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xff, 0xBF,
    0xC6, // C
    0x8C, // P
    0x88  // A
};

u8 dspbuf[8] = {10, 10, 10, 10, 10, 10, 10, 10};
u8 dspcom = 0;

/*************** DS18B20 ***************/
sbit DQ = P1 ^ 4;
float temp_val = 25.0;
u8 temp_update_flag = 0;
u16 time_cnt = 0;

/*************** 系统变量 ***************/
u8 display_mode = 0; // 0:温度,1:参数,2:DAC
u8 tpcs = 25;        // 设定温度
u8 tmp_tpcs = 25;    // 临时设定
u8 mode = 0;         // 0:模式1(开关),1:模式2(线性)
u16 dac_vol = 0;     // 电压值*100

/*************** 单总线延时 ***************/
void Delay_OneWire(u16 t) {
  u8 i;
  while (t--)
    for (i = 0; i < 12; i++)
      ;
}

/*************** DS18B20 驱动（您的原版） ***************/
bit init_ds18b20(void) {
  bit initflag;
  DQ = 1;
  Delay_OneWire(12);
  DQ = 0;
  Delay_OneWire(80);
  DQ = 1;
  Delay_OneWire(10);
  initflag = DQ;
  Delay_OneWire(5);
  return initflag;
}

void Write_DS18B20(u8 dat) {
  u8 i;
  for (i = 0; i < 8; i++) {
    DQ = 0;
    DQ = dat & 0x01;
    Delay_OneWire(5);
    DQ = 1;
    dat >>= 1;
  }
}

u8 Read_DS18B20(void) {
  u8 i, dat = 0;
  for (i = 0; i < 8; i++) {
    DQ = 0;
    dat >>= 1;
    DQ = 1;
    if (DQ)
      dat |= 0x80;
    Delay_OneWire(5);
  }
  return dat;
}

float rd_temperature_f(void) {
  u16 temp;
  float temperature;
  u8 low, high;

  init_ds18b20();
  Write_DS18B20(0xCC);
  Write_DS18B20(0x44);
  Delay_OneWire(200);

  init_ds18b20();
  Write_DS18B20(0xCC);
  Write_DS18B20(0xBE);

  low = Read_DS18B20();
  high = Read_DS18B20();

  temp = (high << 8) | low;
  temperature = temp * 0.0625;
  return temperature;
}

/*************** LED 控制（L1~L4） ***************/
void LED_Control(void) {
  u8 led = 0xFF; // 全灭（低电平有效）
  if (mode == 0)
    led &= 0xFE; // L1亮（模式1）
  if (display_mode == 0)
    led &= 0xFD; // L2亮（温度界面）
  if (display_mode == 1)
    led &= 0xFB; // L3亮（参数界面）
  if (display_mode == 2)
    led &= 0xF7; // L4亮（DAC界面）

  P2 = (P2 & 0x1F) | 0x80;
  P0 = led;
  P2 &= 0x1F;
}

/*************** 显示刷新（填充缓冲区） ***************/
void Display_All(void) {
  u8 i;
  for (i = 0; i < 8; i++)
    dspbuf[i] = 10;

  if (display_mode == 0) // 温度界面：显示 "C  xx.xx"
  {
    int t_int = (int)temp_val;
    int t_dec = (int)((temp_val - t_int) * 100);
    if (t_dec < 0)
      t_dec = 0;
    dspbuf[0] = 12;             // 'C'
    dspbuf[4] = t_int / 10;     // 十位
    dspbuf[5] = t_int % 10;     // 个位
    dspbuf[6] = t_dec / 10;     // 小数第一位
    dspbuf[7] = t_dec % 10;     // 小数第二位
  } else if (display_mode == 1) // 参数界面：显示 "P  tpcs"
  {
    dspbuf[0] = 13; // 'P'
    dspbuf[6] = tmp_tpcs / 10;
    dspbuf[7] = tmp_tpcs % 10;
  } else // DAC界面：显示 "A  x.xx"
  {
    dspbuf[0] = 14;            // 'A'
    dspbuf[5] = dac_vol / 100; // 整数部分
    dspbuf[6] = (dac_vol % 100) / 10;
    dspbuf[7] = dac_vol % 10;
  }
}

/*************** 数码管扫描（动态扫描，修正小数点逻辑） ***************/
void display(void) {
  u8 seg;

  // 消隐
  P2 = (P2 & 0x1F) | 0xE0;
  P0 = 0xFF;
  P2 &= 0x1F;

  // 位选
  P2 = (P2 & 0x1F) | 0xC0;
  P0 = (1 << dspcom);
  P2 &= 0x1F;

  // 获取段码，并根据界面和位决定是否加小数点
  seg = tab[dspbuf[dspcom]];
  if ((display_mode == 0 && dspcom == 5) || // 温度界面：个位加点
      (display_mode == 2 && dspcom == 5))   // DAC界面：整数位加点
  {
    seg &= 0x7F; // 加小数点（共阳：位清零）
  }

  // 输出段码
  P2 = (P2 & 0x1F) | 0xE0;
  P0 = seg;
  P2 &= 0x1F;

  if (++dspcom == 8)
    dspcom = 0;
}

/*************** 按键扫描（矩阵，兼容您的板子） ***************/
u8 key_scan(void) {
  u8 key = 0;
  P3 &= ~0x0C;
  P4 |= 0x14;

  if ((P4 & 0x14) != 0x14) {
    Delay_OneWire(50); // 简单消抖

    P3 = 0xFB;
    if ((P4 & 0x04) == 0)
      key = 5;
    if ((P4 & 0x10) == 0)
      key = 9;

    P3 = 0xF7;
    if ((P4 & 0x04) == 0)
      key = 4;
    if ((P4 & 0x10) == 0)
      key = 8;

    P3 &= ~0x0C;

    while ((P4 & 0x14) != 0x14)
      ; // 等待释放
  }
  return key;
}

/*************** 定时器中断（1ms） ***************/
void timer0_isr() interrupt 1 {
  display(); // 数码管扫描

  if (++time_cnt >= 500) // 每500ms置位温度更新标志
  {
    time_cnt = 0;
    temp_update_flag = 1;
  }
}

/*************** 主函数 ***************/
void main(void) {
  // 关闭蜂鸣器、LED（初始全灭）
  P2 = (P2 & 0x1F | 0xA0);
  P0 = 0x00;
  P2 &= 0x1F; // 关蜂鸣器
  P2 = (P2 & 0x1F | 0x80);
  P0 = 0xFF;
  P2 &= 0x1F; // 关LED

  // 定时器初始化（1ms）
  AUXR |= 0x80;
  TMOD &= 0xF0;
  TL0 = 0x20;
  TH0 = 0xD1;
  TR0 = 1;
  ET0 = 1;
  IP |= 0x02; // 定时器0最高优先级
  EA = 1;

  // 初始化显示和LED
  Display_All();
  LED_Control();

  while (1) {
    u8 key = key_scan();
    if (key) {
      // 界面切换
      if (key == 8) {
        display_mode = (display_mode + 1) % 3;
        LED_Control(); // 更新LED
      }

      // 参数界面调整设定值
      if (display_mode == 1) {
        if (key == 4 && tmp_tpcs < 99)
          tmp_tpcs++;
        if (key == 5 && tmp_tpcs > 0)
          tmp_tpcs--;
      }

      // 模式切换并保存参数
      if (key == 9) {
        mode = !mode;
        tpcs = tmp_tpcs;
        LED_Control(); // 更新LED（L1状态变化）
      }

      Display_All(); // 刷新显示
    }

    // 定时更新温度（每500ms触发一次标志）
    if (temp_update_flag) {
      temp_val = rd_temperature_f(); // 读取DS18B20

      // 根据当前模式计算DAC电压
      if (mode == 0) // 开关模式
      {
        if (temp_val < tpcs)
          dac_vol = 0;
        else
          dac_vol = 500;
      } else // 线性模式
      {
        if (temp_val <= tpcs)
          dac_vol = 0;
        else {
          dac_vol = (u16)((temp_val - tpcs) * 100);
          if (dac_vol > 400)
            dac_vol = 400;
        }
      }

      Display_All(); // 刷新显示
      temp_update_flag = 0;
    }
  }
}