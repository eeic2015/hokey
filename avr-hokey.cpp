/*
  PIN assign
  
  (for LED bar)
  PB2
  PB3
  PB4
  PB5
  PC0
  PC1
  PC2
  PC3
  PC4
  PC5

  (for 7seg LED)
  PD1 A
  PD0 B
  PD7 C
  PD6 D
  PD5 E
  PD3 F
  PD2 G
  PB6 CATHODE1(10 Scale)
  PB7 CATHODE2(1 Scale)
  
  (for Switch)
  PC6 RESET(negative active)
  PB1 for Playing
  PB0 for Hi-Score Delete
  PD4 for Hi-Score display
  
 */

#include <stdint.h>
#include <stdlib.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/eeprom.h>

constexpr int MAX_SCORE = 99;

// 1秒に何回タイマ割り込みが起こるか
constexpr int FRAME_PER_SEC = 500;

// タイマ割り込みのたびに1増えるカウンタ
uint32_t global_timer = 0;

// 配列。最低限の機能のみ
template <class T, int N>
struct array
{
	T elem[N];

	T& operator [] (int i) {
		return elem[i];
	}
	const T& operator [] (int i) const {
		return elem[i];
	}

	T* begin() {
		return elem;
	}
	const T* begin() const {
		return elem;
	}
	T* end() {
		return elem + N;
	}
	const T* end() const {
		return elem + N;
	}
};

// 出力ピン。入出力方向は別途設定する必要がある。
class output_pin
{
public:
	output_pin(volatile uint8_t* port, uint8_t bit) : m_port(port), m_bit(bit) {}

	void set() {
		*m_port = static_cast<uint8_t>(*m_port | _BV(m_bit));
	}

	void reset() {
		*m_port = static_cast<uint8_t>(*m_port & ~_BV(m_bit));
	}

private:
	volatile uint8_t* m_port;
	uint8_t m_bit;
};

// 入力ピン
class input_pin
{
public:
	input_pin(volatile uint8_t* pin, uint8_t bit) : m_pin(pin), m_bit(bit) {}

	bool read() {
		return (*m_pin & _BV(m_bit)) != 0;
	}

private:
	volatile uint8_t* m_pin;
	uint8_t m_bit;
};

namespace seven_segments_data
{
	static constexpr uint8_t A = 0x01;
	static constexpr uint8_t B = 0x02;
	static constexpr uint8_t C = 0x04;
	static constexpr uint8_t D = 0x08;
	static constexpr uint8_t E = 0x10;
	static constexpr uint8_t F = 0x20;
	static constexpr uint8_t G = 0x40;

	static constexpr uint8_t segment_data[10]
	{
		A | B | C | D | E | F,
		B | C,
		A | B | D | E | G,
		A | B | C | D | G,
		B | C | F | G,
		A | C | D | F | G,
		A | C | D | E | F | G,
		A | B | C | F,
		A | B | C | D | E | F | G,
		A | B | C | D | F | G
	};
}

// 7セグ一桁分を表すクラス
class seven_segments
{
public:
	seven_segments(const array<output_pin, 7>& pin) : m_pin(pin) {}

	void set_number(int n) {
		if (n < 0 || n >= 10) return;
		for (int i = 0; i < 7; ++i) {
			if ((seven_segments_data::segment_data[n] & _BV(i)) != 0) {
				m_pin[i].set();
			} else {
				m_pin[i].reset();
			}
		}
	}

	void erase_number() {
		for (auto& p : m_pin) {
			p.reset();
		}
	}

private:
	array<output_pin, 7> m_pin;
};

// ダイナミック点灯による複数桁表示。カソードコモン用
template <int Digit>
class seven_segments_dynamic
{
	static_assert(Digit >= 1, "digit must be greater than 1.");
public:
	seven_segments_dynamic(const seven_segments& display, const array<output_pin, Digit> cathode)
		: m_display(display), m_cathode(cathode) {}

	void init() {
		for (auto& p : m_cathode) {
			p.set();
		}
		m_display.erase_number();
	}

	// 変更はchange_digitを呼ぶまで反映されない
	void set_number(uint32_t value) {
		if (value >= pow10(Digit)) {
			erase_number();
			return;
		}
		m_valid = true;
		m_value = value;
	}

	void erase_number() {
		m_valid = false;
		m_display.erase_number();
	}

	void change_digit() {
		m_cathode[m_now_digit].set();
		++m_now_digit;
		if (m_now_digit == Digit) {
			m_now_digit = 0;
		}
		if (m_valid) {
			// 現在の桁を計算し、7セグに表示
			m_display.set_number(static_cast<int>(m_value / pow10(m_now_digit) % 10));
		}
		// カソードコモンなので表示する桁をLowに
		m_cathode[m_now_digit].reset();
	}

private:
	static constexpr uint32_t pow10(int n) {
		return n == 0 ? 1 : 10 * pow10(n - 1);
	}

	seven_segments m_display;
	array<output_pin, Digit> m_cathode;
	bool m_valid = false;
	uint32_t m_value = 0;
	int m_now_digit = 0;
};

seven_segments_dynamic<2> score_display
{
	seven_segments{{{{&PORTD, PD1}, {&PORTD, PD0}, {&PORTD, PD7}, {&PORTD, PD6}, {&PORTD, PD5}, {&PORTD, PD3}, {&PORTD, PD2}}}},
	{{{&PORTB, PB7}, {&PORTB, PB6}}}
};

// LEDアレイ
class game_bar
{
public:
	game_bar(const array<output_pin, 10>& pin) : m_pin(pin) {}

	void init() {
		for (auto& p : m_pin) {
			p.set();
		}
	}

	void set_position(int pos) {
		if (pos < 0 || pos >= 10) return;
		if (m_pos != BAR_INVALID) {
			m_pin[m_pos].set();
		}
		m_pos = pos;
		m_pin[pos].reset();
	}

	void erase() {
		if (m_pos == BAR_INVALID) return;
		m_pin[m_pos].set();
		m_pos = BAR_INVALID;
	}

private:
	array<output_pin, 10> m_pin;
	static constexpr int BAR_INVALID = -1;    // -1をトラップ表現(バー非表示)として使う
	int m_pos = BAR_INVALID;
};

game_bar bar{{{{&PORTB, PB2}, {&PORTB, PB3}, {&PORTB, PB4}, {&PORTB, PB5}, {&PORTC, PC0}, {&PORTC, PC1}, {&PORTC, PC2}, {&PORTC, PC3}, {&PORTC, PC4}, {&PORTC, PC5}}}};

input_pin game_switch{&PINB, PB1};
input_pin high_score_switch{&PIND, PD4};
input_pin erase_score_switch{&PINB, PB0};

// ハイスコア管理。Singleton
class high_score_manager
{
public:
	static high_score_manager& instance() {
		static high_score_manager object;
		return object;
	}

	uint8_t get_high_score() const {
		return m_high_score;
	}

	void update_high_score(uint8_t score) {
		if (score > m_high_score) {
			m_high_score = score;
			eeprom_busy_wait();
			eeprom_write_byte(&high_score_eeprom, m_high_score);
		}
	}

	void erase_hight_score() {
		if (m_high_score == 0) return;
		m_high_score = 0;
		eeprom_busy_wait();
		eeprom_write_byte(&high_score_eeprom, m_high_score);
	}

private:
	high_score_manager() {
		eeprom_busy_wait();
		m_high_score = eeprom_read_byte(&high_score_eeprom);
	}

	static uint8_t high_score_eeprom EEMEM;
	uint8_t m_high_score;
};

uint8_t high_score_manager::high_score_eeprom EEMEM = 0;

// ゲーム管理。Singleton
class game_manager
{
public:
	static game_manager& instance() {
		static game_manager object;
		return object;
	}

	void update() {
		(this->*m_update_func)();
	}

private:
	game_manager() : m_update_func(&game_manager::ready_to_start) {}

	void init_game() {
		srand(static_cast<unsigned int>(global_timer));

		m_score = 0;
		m_position = 0;
		m_bar_count = 0;
		m_bar_speed_recip = calc_speed_recip();
		m_button_invalid_time = 0;
	}

	int calc_speed_recip() {
		int value = ((30 - m_score / 5) * (80 + rand() % 40) + 50) / 100;
		if (value <= 0) return 1;
		return value;
	}

	void ready_to_start() {
		score_display.set_number(0);
		bar.set_position(0);
		if (!erase_score_switch.read()) {
			high_score_manager::instance().erase_hight_score();
		}
		if (!game_switch.read()) {
			init_game();
			m_update_func = &game_manager::playing;
		} else if (!high_score_switch.read()) {
			m_update_func = &game_manager::show_high_score;
		}
	}

	void show_high_score() {
		score_display.set_number(high_score_manager::instance().get_high_score());
		if (!erase_score_switch.read()) {
			high_score_manager::instance().erase_hight_score();
		}
		if (!game_switch.read()) {
			init_game();
			m_update_func = &game_manager::playing;
		}
	}

	void playing() {
		score_display.set_number(static_cast<uint32_t>(m_score));
		if (m_position < 10) {
			bar.set_position(m_position);
		} else if (m_position < 19) {
			bar.set_position(19 - 1 - m_position);
		} else {
			bar.set_position(0);
		}
		++m_bar_count;
		if (m_bar_count >= m_bar_speed_recip) {
			m_bar_count = 0;
			++m_position;
			if (m_position >= 19) {
				if (m_score > high_score_manager::instance().get_high_score()) {
					m_update_high_score = true;
					high_score_manager::instance().update_high_score(static_cast<uint8_t>(m_score));
				} else {
					m_update_high_score = (m_score == MAX_SCORE);
				}
				m_update_func = &game_manager::show_score_blink;
				m_blink_count = 0;
				return;
			}
		}
		if (m_position >= 16 && m_button_invalid_time == 0 && !game_switch.read()) {
			++m_score;
			if (m_score > MAX_SCORE) m_score = MAX_SCORE;
			m_position = 0;
			m_bar_count = 0;
			m_bar_speed_recip = calc_speed_recip();
		}
		if (!game_switch.read()) {
			m_button_invalid_time = FRAME_PER_SEC / 10;
		} else if (m_button_invalid_time > 0) {
			--m_button_invalid_time;
		}
	}

	void show_score_blink() {
		if (m_blink_count % FRAME_PER_SEC < FRAME_PER_SEC / 2) {
			score_display.set_number(static_cast<uint32_t>(m_score));
		} else {
			score_display.erase_number();
		}
		if (m_blink_count > FRAME_PER_SEC && m_update_high_score) {
			// ハイスコアをとった場合はバーが暴れる
			if (m_blink_count % (FRAME_PER_SEC / 20) == 0) {
				bar.set_position(rand() % 10);
			}
		} else {
			bar.erase();
		}
		++m_blink_count;
		if (m_blink_count >= FRAME_PER_SEC * 3) {
			m_update_func = &game_manager::show_score;
		}
	}

	void show_score() {
		bar.set_position(0);
		score_display.set_number(static_cast<uint32_t>(m_score));
		if (!erase_score_switch.read()) {
			high_score_manager::instance().erase_hight_score();
		}
		if (!game_switch.read()) {
			init_game();
			m_update_func = &game_manager::playing;
		} else if (!high_score_switch.read()) {
			m_update_func = &game_manager::show_high_score;
		}
	}

	// update関数から呼ばれる関数。状態遷移用
	void (game_manager::*m_update_func)();

	int m_score;
	int m_position;    // バーの位置。0～20。10以降が帰り道。18と19(と0)は同じ位置。17, 18, 19の時にボタンを押せば成功
	int m_bar_count;
	int m_bar_speed_recip;    // 待ち時間(速さの逆数)であることに注意

	// チャタリング防止かつ連打防止のため、ボタンを離したあと一定時間ボタンを無効にするためのカウンタ
	int m_button_invalid_time;

	bool m_update_high_score;    // ハイスコアをとったかどうか
	int m_blink_count;    // スコア表示用
};

// 初期化
void io_init();
void timer_init();

// 割り込みベクタ
ISR(TIMER0_OVF_vect)
{
	++global_timer;
	if (global_timer % 4 == 0) {
		score_display.change_digit();
	}
	game_manager::instance().update();
}

int main()
{
	io_init();
	timer_init();
	bar.init();
	score_display.init();
	sei();
	while (true) {}
	return 0;
}

void io_init()
{
	DDRD = 0xEF;    // D4のみ入力
	PORTD |= 0x10;    // D4をプルアップ
	DDRB = 0xFC;    // B0, B1のみ入力
	PORTB |= 0x03;   // B0, B1をプルアップ
	DDRC |= 0x3F;
}

void timer_init()
{
	TCCR0A = 0;
	TCCR0B = _BV(CS01) | _BV(CS00);
	TIMSK0 |= _BV(TOIE0);
}
