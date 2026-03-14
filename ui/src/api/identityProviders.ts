import { get, post, put, del } from './client'
import type {
  IdentityProvider,
  IdentityProviderCreate,
  IdentityProviderUpdate,
  IdpTestResult,
  EnabledIdp,
} from '../types'

export function listIdentityProviders(): Promise<IdentityProvider[]> {
  return get('/identity-providers')
}

export function getIdentityProvider(id: number): Promise<IdentityProvider> {
  return get(`/identity-providers/${id}`)
}

export function createIdentityProvider(
  data: IdentityProviderCreate,
): Promise<{ id: number; name: string }> {
  return post('/identity-providers', data)
}

export function updateIdentityProvider(
  id: number,
  data: IdentityProviderUpdate,
): Promise<{ message: string }> {
  return put(`/identity-providers/${id}`, data)
}

export function deleteIdentityProvider(id: number): Promise<void> {
  return del(`/identity-providers/${id}`)
}

export function testIdentityProvider(id: number): Promise<IdpTestResult> {
  return get(`/identity-providers/${id}/test`)
}

export function generateSpKeyPair(entityId: string): Promise<{ private_key: string; certificate: string }> {
  return post('/identity-providers/generate-sp-keypair', { entity_id: entityId })
}

export function listEnabledIdps(): Promise<EnabledIdp[]> {
  return get('/auth/identity-providers')
}
