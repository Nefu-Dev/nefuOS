import base64
samples = ["FIkttSiCJ7Ukmj8=", "J4Uoiz+DJIQ=", "I5gujA==", "O584gg==", "J48ljT+C",
           "OIIijD8=", "OI8ljg==", "Op8umDK5LoYuiT+FOQ==", "O485jCSYJosliS4=",
           "LI8/ryWeOYMumQ==", "Egk1AX4NdQ==", "LlUsSj51JEo5", "LF0p", "Klw5fCNNP1AoSg==",
           "IVwjXjlR"]
for s in samples:
    try:
        b = base64.b64decode(s, validate=False)
    except Exception as e:
        print(s, "ERR", e)
        continue
    print(repr(s), len(b), repr(b))
