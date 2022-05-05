import bpy
import bmesh

print([[int(x) for x in p.vertices] for p in bpy.context.active_object.data.polygons])
print('\n'.join([str(x.co) for x in bpy.context.active_object.data.vertices]))
