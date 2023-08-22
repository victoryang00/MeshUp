import os, subprocess

for i in range(1, 10000000):
    cmd = "./core2cha_layout " + str(56321375031263575 + i) + "|sort|uniq|wc -l"
    ps = subprocess.Popen(
        cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    output = ps.communicate()[0]
    print(i,output)
    if int(output) > 20:
        with open("correct.txt", "a") as f:
            f.write(str(56321375031263575 + i) + "\n")
