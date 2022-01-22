import json
import struct

out = bytearray()

kinds = [
    { "inJson": "trees", "name": "tree", "floats": 2, "fields": ['x', 'y'] },
]

with open('build/map.h', 'w') as f:
    f.write("#include <arpa/inet.h>\n\n");
    
    for kd in kinds:
        f.write(f"typedef struct {{" + '\n')
        for field in kd['fields']:
            f.write(f"    float {field};" + '\n')
        f.write(f"}} MapData_{kd['name'].capitalize()};" + '\n')

    f.write('typedef struct {\n')
    for kd in kinds:
        f.write(f"    MapData_{kd['name'].capitalize()} *{kd['inJson']};" + '\n');
        f.write(f"    uint32_t n{kd['inJson']};" + '\n');
    f.write('} MapData;\n\n')

    f.write('static MapData parse_map_data(FILE *f) {\n');
    f.write('    MapData md = {0};\n');
    f.write('    uint32_t sec_len = 0;\n');
    for kd in kinds:
        data_type = 'MapData_' + kd['name'].capitalize()
        f.write(f"    if (fread(&sec_len, sizeof(uint32_t), 1, f) < 1)\n")
        f.write(f'        perror("couldn\'t get map data section length"), exit(1);\n')
        f.write(f"    sec_len = md.n{kd['inJson']} = ntohl(sec_len);\n")
        f.write(f"    if (fread(\n")
        f.write(f"        md.{kd['inJson']} = calloc(sizeof({data_type}), sec_len),\n")
        f.write(f"        sizeof({data_type}),\n"),
        f.write(f"        sec_len,\n")
        f.write(f"        f\n")
        f.write(f'    ) < sec_len) perror("less map data than section length"), exit(1);\n')
    f.write('    return md;\n');
    f.write('}');


with open("map.json", "r") as f:
    data = json.loads(f.read())
    for kd in kinds:
        inJson = data[kd['inJson']]
        out += struct.pack('!L', len(inJson))
        for pl in inJson:
            out += struct.pack('f'*kd['floats'],  *pl)

with open('build/map.bytes', 'wb') as f:
    f.write(out)

print("done!")
