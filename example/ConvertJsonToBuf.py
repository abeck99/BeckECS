import sys
sys.path.append("PyClientExample/Messages")
import protobufHelper as p
import json

jsonFileName = sys.argv[1]
f = open(jsonFileName)
j = json.loads(f.read())
f.close()
s = p.ConvertEntityDictToProtobuf(j, "PyClientExample/Components")
f = open(sys.argv[2], "wb")
f.write(s.SerializeToString())
f.close()
