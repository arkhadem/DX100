import json

flag_files = ["tests/test-data/flag/001.nonfp.json",
            "tests/test-data/flag/001.fp.json",
            "tests/test-data/flag/001.json"]

all_data = []
for file in flag_files:
    with open(file, 'r') as file:
        data = json.load(file)
        all_data += data

for d in all_data:
    print(len(d["pattern"]))

# Step 3: Store the modified data back to the file
with open('tests/test-data/flag/all.json', 'w') as file:
    json.dump(all_data, file, indent=4)

all_data = None
with open("tests/test-data/xrage/spatter.json", 'r') as file:
    all_data = json.load(file)

for didx in range(len(all_data)):
    print(f'processing pattern size {len(all_data[didx]["pattern"])}')
    if len(all_data[didx]["pattern"]) > 2 * 1024 * 1024:
        all_data[didx]["pattern"] = all_data[didx]["pattern"][:2 * 1024 * 1024]
        print(f'pattern size reduced to {len(all_data[didx]["pattern"])}')

with open('tests/test-data/xrage/all.json', 'w') as file:
    json.dump(all_data, file, indent=4)
