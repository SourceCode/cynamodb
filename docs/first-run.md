# First Run Guide

Follow these steps to get cynamoDB up and running and perform your first DynamoDB operations.

## 1. Start the Server

After building the project, start the cynamoDB server in a terminal:

```bash
./build/cynamodb --port 8000 --data-dir ./data
```

You should see an output indicating the server is listening:
`[info] cynamoDB v2.0.0 listening on 127.0.0.1:8000`

## 2. Verify Connectivity

In a separate terminal, use the AWS CLI to ping the local server. We use the `--endpoint-url` flag to redirect traffic from AWS to our local instance.

```bash
aws dynamodb list-tables --endpoint-url http://localhost:8000 --region us-east-1
```

**Expected Response**:
```json
{
    "TableNames": []
}
```

## 3. Create Your First Table

Create a simple table named `Music` with a partition key `Artist` and a sort key `SongTitle`.

```bash
aws dynamodb create-table \
    --table-name Music \
    --attribute-definitions \
        AttributeName=Artist,AttributeType=S \
        AttributeName=SongTitle,AttributeType=S \
    --key-schema \
        AttributeName=Artist,KeyType=HASH \
        AttributeName=SongTitle,KeyType=RANGE \
    --provisioned-throughput \
        ReadCapacityUnits=5,WriteCapacityUnits=5 \
    --endpoint-url http://localhost:8000
```

## 4. Add an Item

Put a record into the `Music` table.

```bash
aws dynamodb put-item \
    --table-name Music \
    --item \
        '{"Artist": {"S": "No One You Know"}, "SongTitle": {"S": "Call Me Today"}, "AlbumTitle": {"S": "Somewhat Famous"}}' \
    --endpoint-url http://localhost:8000
```

## 5. Retrieve the Item

Get the item back using its primary key.

```bash
aws dynamodb get-item \
    --table-name Music \
    --key '{"Artist": {"S": "No One You Know"}, "SongTitle": {"S": "Call Me Today"}}' \
    --endpoint-url http://localhost:8000
```

## 6. Cleanup

To stop the server, press `Ctrl+C` in the server terminal window. Your data will persist in the `./data` directory unless you delete it.

### Reset Everything
To start fresh, simply delete the data directory:
```bash
rm -rf ./data
```
