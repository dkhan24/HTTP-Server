#!/bin/bash

PORT=$@

REQUEST=$'GET /ping HTTP/1.1\r\n\r\n'

echo -n "$REQUEST" | nc 127.0.0.1 $PORT
