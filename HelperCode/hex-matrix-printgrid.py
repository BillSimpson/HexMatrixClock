from PIL import Image, ImageDraw, ImageFont
import math
import numpy as np
import datetime

# Show the coordinates or not
show_coords = True
#show_coords = False

# Use the following offsets to shift the origin for the printed coordinates
q_c = 7
r_c = -2

# This prints out the matrix that is 13 hexels wide, 4/5 hexels high
# in a flat-topped hexagonal display
# this is scaled for flexible pixels per inch printing
dpi = 600

# linewidth in pixels
linewidth = 3

# Image will be centered on a page of these dimensions
x_inches = 17
y_inches = 11

# scale factor should be 1 to put the outline at the edge
scale_factor = 1.000

# display is defined by the half L length in pixels = hal
hexside = 1.312 / math.sqrt(3)
print ('Hex edge length in inches = {:.3f}'.format(hexside))
hal = round ( dpi * hexside / 2 ) 
print ('Half length of side in pixels = {:} at {:} dpi'.format(hal,dpi))
#hal = 57 is the result for 150 dpi

# we also need the sqrt(3) times L/2 = r3hal
r3hal = round(math.sqrt(3)*hal)

# the full dimensions of the space is 40*hal x 10*r3hal
x_max = 40*hal
y_max = 10*r3hal
x_pixels = round ( dpi * x_inches )
y_pixels = round ( dpi * y_inches )
left_space = (x_pixels - x_max) / 2
top_space = (y_pixels - y_max) / 2

# set full image dimension
cart_size = (x_pixels, y_pixels)

# the cartesian coordinates are offset slightly
cart_origin = (2*hal + left_space , 2*r3hal + top_space)

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

cart_hex_vertices = [(-2*hal, 0), (-1*hal,-1*r3hal), (hal,-1*r3hal), (2*hal,0), (hal,r3hal), (-1*hal,r3hal), (-2*hal, 0)]

def hex2cart_polygon(q, r, scale):
    cart_vertices = []
    for cv in cart_hex_vertices:
        cart_vertices.append(add_coords(hex2cart(q,r),scale_coords(cv,scale)))
    return(cart_vertices)

# print out the full size
len_in, wid_in = scale_coords(cart_size,1/dpi)
print('image dimensions = {:.3f} x {:.3f} inches, landscape'.format(len_in, wid_in))
print('vertical spacing between rows {:.3f} inches'.format(2*r3hal / dpi))
print('horizontal spacing between columns {:.3f} inches'.format(3*hal / dpi))

# draw the image
im = Image.new('RGB',cart_size, 'white')
draw = ImageDraw.Draw(im)

# draw glass bounding box
glass_x_inches = 17
glass_y_inches = 10
glass_x_pixels = round ( dpi * glass_x_inches )
glass_y_pixels = round ( dpi * glass_y_inches )
glass_half_x = glass_x_pixels / 2
glass_half_y = glass_y_pixels / 2
page_center = (x_pixels/2, y_pixels/2)
gbb = [add_coords(page_center,(-glass_half_x,-glass_half_y)),
       add_coords(page_center,(glass_half_x,-glass_half_y)),
       add_coords(page_center,(glass_half_x,glass_half_y)),
       add_coords(page_center,(-glass_half_x,glass_half_y)),
       add_coords(page_center,(-glass_half_x,-glass_half_y))]
draw.line(gbb,fill='black',width = linewidth)

# get the font
fnt = ImageFont.truetype("Arial.ttf", 120)

# to map out all characters, you start at q=0, r=0
# then go down, increasing r
r = 0
for q in range(13):
    if q % 2 == 0:   # even column grows downward for 4 hexels
        for ix in range(4):
            cv = hex2cart_polygon(q, r, scale_factor)
            draw.line(cv,fill='black',width = linewidth)
            textcoords = '({:},{:})'.format(q-q_c,r-r_c)
            w, h = draw.textsize(textcoords, font=fnt)
            if show_coords:
                draw.text(add_coords((-w/2,-h/2),hex2cart(q,r)), textcoords, font=fnt, fill='black')
            r+=1
    else:         # odd columns go upward for 5 hexels
        for ix in range(5):
            r-=1
            cv = hex2cart_polygon(q, r, scale_factor)
            draw.line(cv,fill='black',width = linewidth)
            textcoords = '({:},{:})'.format(q-q_c,r-r_c)
            w, h = draw.textsize(textcoords, font=fnt)
            if show_coords:
                draw.text(add_coords((-w/2,-h/2),hex2cart(q,r)), textcoords, font=fnt, fill='black')

# use some of the code below to printout a light-sensor hole
#cv = hex2cart_polygon(6,-3.4, 0.07)
#draw.polygon(cv,fill='white')     # for white fill
#draw.polygon(cv,fill='#202020')    # for grey fill

            
im.save('hex-matrix-grid.png', dpi=(dpi,dpi))


box = (0, 0, x_pixels/2, y_pixels)
crop = im.crop(box)
crop.save('hex-matrix-grid-half.png', dpi=(dpi,dpi))

