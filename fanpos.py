#円周上に並ぶLEDの位置をC言語のコードとして出力するスクリプト
import math

FAN_WIDTH_MM = 120
FAN_HEIGHT_MM = 120
FAN_LED_RADIUS = 30

FAN_LED_COUNT = 8

for led_no in range(FAN_LED_COUNT):
	rad = math.radians(90 + 22.5) + (2.0 * math.pi / FAN_LED_COUNT) * led_no
	cx = 0
	cy = 0
	x = cx + FAN_LED_RADIUS * math.cos(rad)
	y = cy + FAN_LED_RADIUS * math.sin(rad)

	x = math.floor(x + 0.5)
	y = math.floor(y + 0.5)

	print("{{{0: >3},{1: >3}}},".format(x,y))