from PIL import Image, ImageDraw
import math
import numpy as np
import datetime

dpi = 150

# these bitmasks are 13 bit integers and are interpreted as the 13 bits
# in a flat-topped hexagonal display using 3 columns that have 4, 5, 4
# vertical bits.  The ordering of those bits are the least significant
# bit is the top left, and it then reads down the left column, up the
# middle column, then down the left column.  The most significant bit
# is the bottom right.

char_bitmasks = [
	0b1111100011111,
	0b0000111110000,
	0b1011101011101,
	0b1111101011001,
	0b1111001000011,
	0b1101101011011,
	0b1101101011111,
	0b0111100110001,
	0b1111101011111,
	0b1111101011011,
        0b1111101001111,  # A
        0b1100001011111,  # b
        0b1001100011111,  # C
        0b1111001011100,  # d
        0b1001101011111,  # E
        0b0001101001111   # F
]

# to read a bit, you can use this function:
def bit_lit(char_bitmask, bit):
	return char_bitmask & 1<<bit != 0

# display is defined by the half L length in pixels = hal
hal = 15

# we also need the sqrt(3) times L/2 = r3hal
r3hal = round(math.sqrt(3)*hal)

# the full dimensions of the space is 96*hal x 22*r3hal
x_max = 96*hal
y_max = 22*r3hal
cart_size = (x_max, y_max)

# the cartesian coordinates are offset slightly
cart_origin = (-3*hal , 0)

# note that the first digit is only 1 or 0, so there are three other digits
# offsets for placing the three later digits
digit_origins = []
for i in range(16):
    if i < 8:
        digit_origins.append( (2+i*4, -2*i) )
    else:
        digit_origins.append( (2+(i-8)*4, 6-2*(i-8)) )

# for reading the character bitmaps into hex coordinates, we follow char_hex_offsets order
char_hex_offsets = [(0,0), (0,1), (0,2), (0,3), (1,3), (1,2), (1,1), (1,0), (1,-1), (2,-1), (2,0), (2,1), (2,2)]

def add_coords(pt1, pt2):
    x1, y1 = pt1
    x2, y2 = pt2
    return((x1+x2,y1+y2))

def scale_coords(pt, scale):
    x, y = pt
    return((scale*x,scale*y))

def hex2cart(q,r):
    x = 3*hal*q
    y = r3hal*(q+2*r)
    return(add_coords((x,y),cart_origin))

cart_hex_vertices = [(-2*hal, 0), (-1*hal,-1*r3hal), (hal,-1*r3hal), (2*hal,0), (hal,r3hal), (-1*hal,r3hal)]

def hex2cart_polygon(q, r, scale):
    cart_vertices = []
    for cv in cart_hex_vertices:
        cart_vertices.append(add_coords(hex2cart(q,r),scale_coords(cv,scale)))
    return(cart_vertices)

im = Image.new('RGBA' , cart_size, 'black')
draw = ImageDraw.Draw(im)

# testing printing hex hex font

for digit in range(16):
    ori = digit_origins[digit]
    for iy, char_offset in enumerate(char_hex_offsets):
        if bit_lit(char_bitmasks[digit],iy):
            q, r = add_coords(ori,char_offset)
            cv = hex2cart_polygon(q, r, 0.8)
            draw.polygon(cv,fill='white')


im.save('hex-matrix-font.png', dpi=(dpi,dpi))


