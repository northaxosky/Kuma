"""Generate a minimal multi-node glTF fixture for kuma-bake scene tests.

Creates two_node_scene.glb with:
  - 1 mesh with 1 primitive (a triangle in the XY plane)
  - 2 scene nodes both referencing that mesh
  - Node A at identity, node B translated +5 on X
This exercises the bake_scene primitive-dedup path: one .kmesh emitted,
two node entries pointing at it with different world transforms.
"""
import json
import os
import struct

# ── Vertex buffer: 3 verts, interleaved pos(12) + normal(12) + uv(8) = 32
verts = []
for p in [(-1.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 2.0, 0.0)]:
    verts.extend(p)
    verts.extend((0.0, 0.0, 1.0))  # normal +Z
    verts.extend((0.0, 0.0))       # uv

verts_bytes = struct.pack('<%df' % len(verts), *verts)
indices_bytes = struct.pack('<3H', 0, 1, 2)
while len(indices_bytes) % 4:
    indices_bytes += b'\x00'
buffer_bytes = verts_bytes + indices_bytes

gltf = {
    'asset': {'version': '2.0'},
    'scene': 0,
    'scenes': [{'nodes': [0, 1]}],
    'nodes': [
        {'mesh': 0, 'name': 'A'},
        {'mesh': 0, 'name': 'B', 'translation': [5.0, 0.0, 0.0]},
    ],
    'meshes': [{'primitives': [{
        'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
        'indices': 3,
        'mode': 4,  # TRIANGLES
    }]}],
    'buffers': [{'byteLength': len(buffer_bytes)}],
    'bufferViews': [
        {'buffer': 0, 'byteOffset': 0, 'byteLength': len(verts_bytes), 'byteStride': 32, 'target': 34962},
        {'buffer': 0, 'byteOffset': len(verts_bytes), 'byteLength': len(indices_bytes), 'target': 34963},
    ],
    'accessors': [
        {'bufferView': 0, 'byteOffset': 0,  'componentType': 5126, 'count': 3, 'type': 'VEC3',
         'min': [-1.0, 0.0, 0.0], 'max': [1.0, 2.0, 0.0]},
        {'bufferView': 0, 'byteOffset': 12, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
        {'bufferView': 0, 'byteOffset': 24, 'componentType': 5126, 'count': 3, 'type': 'VEC2'},
        {'bufferView': 1, 'componentType': 5123, 'count': 3, 'type': 'SCALAR'},
    ],
}

json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
while len(json_bytes) % 4:
    json_bytes += b' '

total_len = 12 + 8 + len(json_bytes) + 8 + len(buffer_bytes)
glb = struct.pack('<4sII', b'glTF', 2, total_len)
glb += struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
glb += struct.pack('<II', len(buffer_bytes), 0x004E4942) + buffer_bytes

out = os.path.join('tools', 'kuma-bake', 'tests', 'fixtures', 'two_node_scene.glb')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'wb') as f:
    f.write(glb)
print(f'wrote {out}: {len(glb)} bytes')
