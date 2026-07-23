# DPLEvolve reviewer console

The Web Demo provides browser controls for the same fixed Make targets documented
in the repository README.

## Start

From the repository root:

```bash
bash web-demo/start.sh
```

Open `http://127.0.0.1:8080`.

For a remote evaluation server, keep the service bound to loopback and open an
SSH tunnel from the reviewer's computer:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

Then open `http://127.0.0.1:8080` locally.

## Use

The page is organized in two stages:

1. prepare the pinned EDA environment and paper inputs;
2. run Table 4, Table 5, Table 6, the ReviewDSE search, Figures 4/5, or the
   Ariane diagnostic.

Commands are serialized and their output is streamed into the reviewer terminal.
The complete ReviewDSE search requires authenticated model/API access and the
paper-scale token budget. Table 5 reports its known missing-input condition.

The backend accepts predefined task names only. SSH credentials remain in server
memory while tasks exist and are excluded from status and exported session data.
