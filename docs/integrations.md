# SDK and Tool Integrations

One of the primary goals of cynamoDB is to be a drop-in replacement for AWS DynamoDB. It works seamlessly with official AWS SDKs and third-party tools.

## Using with AWS CLI

The AWS CLI is fully compatible. You must provide the `--endpoint-url` pointing to your local cynamoDB instance.

```bash
aws dynamodb list-tables --endpoint-url http://localhost:8000
```

## Using with AWS SDKs

When initializing an SDK client, you must override the endpoint configuration.

### Node.js (AWS SDK v3)
```javascript
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");

const client = new DynamoDBClient({
  region: "us-east-1",
  endpoint: "http://localhost:8000",
  credentials: {
    accessKeyId: "local",
    secretAccessKey: "local"
  }
});
```

### Python (Boto3)
```python
import boto3

dynamodb = boto3.resource(
    'dynamodb',
    endpoint_url='http://localhost:8000',
    region_name='us-east-1',
    aws_access_key_id='local',
    aws_secret_access_key='local'
)
```

### Go (SDK v2)
```go
cfg, err := config.LoadDefaultConfig(context.TODO(),
    config.WithEndpointResolver(aws.EndpointResolverFunc(
        func(service, region string) (aws.Endpoint, error) {
            return aws.Endpoint{URL: "http://localhost:8000"}, nil
        })),
)
```

## Third-Party Tools

### NoSQL Workbench
You can use Amazon's **NoSQL Workbench** to connect to cynamoDB:
1. Open NoSQL Workbench.
2. Go to **Operation builder**.
3. Select **Add connection**.
4. Choose **DynamoDB local**.
5. Enter `http://localhost:8000`.

### Infrastructure as Code (Terraform)
You can use Terraform to manage local tables by setting the `endpoints` block in the provider configuration.

```hcl
provider "aws" {
  region                      = "us-east-1"
  access_key                  = "mock_access_key"
  secret_key                  = "mock_secret_key"
  skip_credentials_validation = true
  skip_metadata_api_check     = true
  skip_requesting_account_id  = true

  endpoints {
    dynamodb = "http://localhost:8000"
  }
}
```

## Limitations in Integration
- **IAM Policies**: cynamoDB does not currently support complex IAM policy evaluation. Any valid SigV4 signature is accepted for any operation.
- **HTTPS**: By default, cynamoDB serves over HTTP. If your client requires HTTPS, you may need a reverse proxy like Nginx or Caddy.
