PYTHONPATH=./scripts python3 -c '
import sim
J = 1                      # <-- change this
DATA_DIR = "./data"       # <-- change this to your -dir
sim.set_data_directory(DATA_DIR)
sim.run_simulation(J, True, True)
target = "/PRO/MAA/2M"   # selects both checkpoint + run dirs
for t in sim.tasks:
    if target not in t.directory:
        t.started = True
        t.finished = True
sim.run_tasks(J)
'