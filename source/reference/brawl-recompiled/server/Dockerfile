FROM python:3.12-slim
COPY brawl_online_server.py /app/
EXPOSE 52200/udp
CMD ["python3", "-u", "/app/brawl_online_server.py", "--host", "fly-global-services", "--port", "52200"]
