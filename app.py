from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from subprocess import run, PIPE
import json

app = FastAPI()

class TokenRequest(BaseModel):
    token: str  # Ensure that the token is being sent as a string

@app.post("/decode_token/")
async def decode_token(request: TokenRequest):
    try:
        # Make sure to convert the token to string if necessary
        command = ['opaygo_decoder', str(request.token)]
        result = run(command, stdout=PIPE, stderr=PIPE, text=True)
        if result.returncode != 0:
            raise Exception(f"Decoder Error: {result.stderr}")

        # Assuming the output is JSON formatted for simplicity
        return json.loads(result.stdout)
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

