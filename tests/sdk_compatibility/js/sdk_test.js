const { DynamoDBClient, ListTablesCommand } = require("@aws-sdk/client-dynamodb");

async function runTest() {
    const client = new DynamoDBClient({
        endpoint: process.env.CYNAMODB_ENDPOINT || "http://localhost:8000",
        region: "us-east-1",
        credentials: { accessKeyId: "test", secretAccessKey: "test" }
    });

    try {
        const response = await client.send(new ListTablesCommand({}));
        console.log("Success:", response);
    } catch (err) {
        console.error("Error:", err);
        process.exit(1);
    }
}

runTest();
