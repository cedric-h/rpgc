from math import sqrt
import bpy

coords = []
faces = []
idx = 0

def draw_line(x0, y0, x1, y1, thickness):
    global idx
    dx = x0 - x1
    dy = y0 - y1
    
    nx = -dy
    ny = dx
    
    tmag = sqrt(nx*nx + ny*ny)
    tx = nx / tmag * (thickness * 0.5)
    ty = ny / tmag * (thickness * 0.5)
    
    coords.append((x0 + tx, y0 + ty, 0.0))
    coords.append((x0 - tx, y0 - ty, 0.0))
    coords.append((x1 + tx, y1 + ty, 0.0))
    coords.append((x1 - tx, y1 - ty, 0.0))
    faces.append((idx+0, idx+1, idx+2))
    faces.append((idx+2, idx+1, idx+3))
    idx += 4

draw_line(-0.10, -1.0, 0.03, -1.1, 0.045)
draw_line(-0.10, -1.0, 0.20, -0.4, 0.105)
draw_line( 0.25, -0.1, 0.20, -0.4, 0.095)
draw_line( 0.25, -0.1, 0.10,  0.0, 0.075)
draw_line( 0.25,  0.1, 0.10,  0.0, 0.075)
draw_line( 0.25,  0.1, 0.20,  0.4, 0.095)
draw_line(-0.10,  1.0, 0.20,  0.4, 0.105)
draw_line(-0.10,  1.0, 0.03,  1.1, 0.045)

mesh = bpy.data.meshes.new(name="MyMesh")

object = bpy.data.objects.new('MESH', mesh)
bpy.context.collection.objects.link(object)

mesh.from_pydata(coords, [], faces)
mesh.update(calc_edges=True)
